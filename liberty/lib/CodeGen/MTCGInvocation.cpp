#define DEBUG_TYPE "mtcg"

#include "liberty/CodeGen/MTCG.h"
#include "liberty/Utilities/AllocaHacks.h"
#include "liberty/Utilities/GepAndLoad.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

/// This method replaces a loop invocation with a code sequence that will
/// perform a parallel invocation and possibly perform non-speculative sequential
/// recovery.
void MTCG::createParallelInvocation(PreparedStrategy &strategy)
{
  DEBUG(errs() << "-------------- Transform main entry -------------\n");
  Loop *loop = strategy.loop;
  const PipelineStrategy::Stages &stages = strategy.lps->stages;
  const VSet &liveIns = strategy.liveIns;
  const PreparedStrategy::StagePairs &queues = strategy.queues;
  PreparedStrategy::Stage2Fcn &functions = strategy.functions;
  BasicBlock *preheader = loop->getLoopPreheader();
  assert( preheader && "No loop preheader; you didn't run loop simplify");
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();
  Module *mod = fcn->getParent();
  LLVMContext &ctx = mod->getContext();
  Api api(mod);

  BasicBlock *should_invoc_bb = BasicBlock::Create(ctx, "should_invoc", fcn);
  BasicBlock *begin_invoc_bb = BasicBlock::Create(ctx, "begin_invoc", fcn);
  BasicBlock *spawn_workers_bb = BasicBlock::Create(ctx, "spawn_workers", fcn);
  BasicBlock *join_workers_bb = BasicBlock::Create(ctx, "join_workers", fcn);
  BasicBlock *end_invoc_bb = BasicBlock::Create(ctx, "end_invoc", fcn);
  BasicBlock *perform_recovery_bb = BasicBlock::Create(ctx, "perform_recovery", fcn);
  BasicBlock *restart_parallel_bb = BasicBlock::Create(ctx, "restart_parallel", fcn);

  should_invoc_bb->moveAfter(preheader);
  begin_invoc_bb->moveAfter(should_invoc_bb);
  spawn_workers_bb->moveAfter(begin_invoc_bb);
  join_workers_bb->moveAfter(spawn_workers_bb);
  end_invoc_bb->moveAfter(join_workers_bb);

  preheader->getTerminator()->replaceUsesOfWith(header, should_invoc_bb);

  Constant *numAvail = api.getNumAvailableWorkers(),
           *begininvoc = api.getBeginInvocation(),
           *currentIter = api.getCurrentIter(),
           *spawn = api.getSpawn(),
           *join = api.getJoin(),
           *endinvoc = api.getEndInvocation();

  // Determine if we should invoke
  Instruction *avail = CallInst::Create(numAvail);
  avail->setName("available");
  ConstantInt *minWorkers = ConstantInt::get( api.getU32(), stages.size());
  Instruction *cmp = new ICmpInst(ICmpInst::ICMP_UGE, avail, minWorkers);
  Instruction *br = BranchInst::Create(begin_invoc_bb, header, cmp);
  InstInsertPt::Beginning(should_invoc_bb)
    << avail
    << cmp
    << br;

  // Update PHIs in loop header
  for(BasicBlock::iterator i=header->begin(), e=header->end(); i!=e; ++i)
  {
    PHINode *phi = dyn_cast< PHINode >( &*i );
    if( !phi )
      continue;
    for(unsigned j=0, N=phi->getNumIncomingValues(); j<N; ++j)
      if( phi->getIncomingBlock(j) == preheader )
        phi->setIncomingBlock(j, should_invoc_bb);
  }

  IntegerType *u32 = api.getU32();
  ConstantInt *zero   = ConstantInt::get(u32,0),
              *one    = ConstantInt::get(u32,1);
//                *negOne = ConstantInt::get(u32,-1);
  IntegerType *u8 = IntegerType::getInt8Ty(ctx);
  Type *voidptr = PointerType::getUnqual( u8 );

  // Create a dispatch function, invoked by spawnWorkers
  SmallVector<Type*,2> argTys;
  argTys.push_back(voidptr);
  FunctionType *fty = FunctionType::get( api.getVoid(), ArrayRef<Type*>(argTys), false);
  Twine name = "__specpriv_pipeline__" + fcn->getName() + "__" + header->getName() + "_dispatch";
  Function *dispatch_function = Function::Create(fty, GlobalValue::InternalLinkage, name, mod);
  dispatch_function->setDoesNotReturn();

  // In the spawn bb, call spawn, then branch either
  // to the loop header, or to the parent bb

  Instruction *callinvoc = CallInst::Create(begininvoc, "numThreads");
  Value *numThreads = callinvoc;

  InstInsertPt S = InstInsertPt::End(begin_invoc_bb);
  S << callinvoc;

  // How many sequential stages are there?
  const unsigned M=functions.size();
  unsigned numSequential=0;
  for(unsigned i=0; i<M; ++i)
    if( stages[i].type == PipelineStage::Sequential )
      ++numSequential;

  SmallVector<Value*,5> args;
  std::vector<Value*>   stage2rep( strategy.numStages() );

  // Create all of the queues
  typedef std::map< PreparedStrategy::StagePair, Value *> QueueObjects;
  QueueObjects queueObjects;
  {
    Instruction *numParallelThreads = BinaryOperator::CreateNSWAdd(numThreads, ConstantInt::get(u32, -numSequential), "num.parallel.stage.threads");
    ConstantInt *numParallelStages = ConstantInt::get(u32, M-numSequential);
    Instruction *threadsPerParallelStageRoundDown = BinaryOperator::Create(Instruction::UDiv, numParallelThreads, numParallelStages, "threads.per.parallel.stage.round.down");
    Instruction *remainder                        = BinaryOperator::Create(Instruction::URem, numParallelThreads, numParallelStages, "remainder");
    Instruction *threadsPerParallelStageRoundUp   = BinaryOperator::Create(Instruction::Add, threadsPerParallelStageRoundDown, remainder, "threads.per.parallel.stage.round.up");
    S << numParallelThreads << threadsPerParallelStageRoundDown << remainder << threadsPerParallelStageRoundUp;

    const unsigned N = strategy.numStages();
    bool first = true;
    for(unsigned i=0; i<N; ++i)
    {
      if( strategy.lps->stages[i].type == PipelineStage::Parallel )
      {
        if( first )
          stage2rep[i] = threadsPerParallelStageRoundUp;
        else
          stage2rep[i] = threadsPerParallelStageRoundDown;

        first = false;
      }
      else
        stage2rep[i] = one;
    }

    Constant *createQueue = api.getCreateQueue();
    for(PreparedStrategy::StagePairs::iterator i=queues.begin(), e=queues.end(); i!=e; ++i)
    {
      args.clear();
      args.push_back( stage2rep[ i->first ] );
      args.push_back( stage2rep[ i->second ] );
      Instruction *create = CallInst::Create(createQueue, ArrayRef<Value*>(args));
      create->setName("q_from_" + Twine(i->first) + "_to_" + Twine(i->second) + "." );
      S << create;
      queueObjects[ *i ] = create;
    }
  }

  // Inform runtime chosen strategy
  {
    args.clear();
    args.push_back( numThreads );

    const unsigned N = strategy.numStages();
    args.push_back( ConstantInt::get( u32, N ) );

    for (unsigned i=0; i<N; i++)
      args.push_back( stage2rep[i] );

    S << CallInst::Create(api.getInformStrategy(), ArrayRef<Value*>(args));
  }

  S << BranchInst::Create(spawn_workers_bb);

  S = InstInsertPt::End(spawn_workers_bb);

  Instruction *calliter  = CallInst::Create(currentIter, "current.iter");
  args.clear();
  args.push_back(calliter);
  args.push_back(dispatch_function);
  args.push_back( UndefValue::get(voidptr) );
  CallInst *callspawn = CallInst::Create(spawn, ArrayRef<Value*>(args));
//    cmp = CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_EQ, callspawn, negOne, "is.main.process?");
//    br = BranchInst::Create(join_workers_bb, invokeChain, cmp);

  S << calliter
    << callspawn
    << BranchInst::Create(join_workers_bb);
//      << cmp
//      << br;

  InstInsertPt saveLiveIns = InstInsertPt::Before(callspawn);

  const Preprocess &preprocessor = getAnalysis< Preprocess >();
  const RecoveryFunction &recovery = preprocessor.getRecoveryFunction(loop);
  const unsigned N = recovery.liveoutStructure.liveouts.size();
  const LiveoutStructure::PhiList &phis = recovery.liveoutStructure.phis;
  Value *object = recovery.liveoutStructure.object;

  BasicBlock *invokeChain = BasicBlock::Create(ctx, "invoke.chain.", dispatch_function);

  S = InstInsertPt::Beginning(dispatch_function);
  Instruction *numParallelThreads = BinaryOperator::CreateNSWAdd(numThreads, ConstantInt::get(u32, -numSequential), "num.parallel.stage.threads");
  ConstantInt *numParallelStages = ConstantInt::get(u32, M-numSequential);
  Instruction *threadsPerParallelStageRoundDown = BinaryOperator::Create(Instruction::UDiv, numParallelThreads, numParallelStages, "threads.per.parallel.stage.round.down");
  Instruction *remainder                        = BinaryOperator::Create(Instruction::URem, numParallelThreads, numParallelStages, "remainder");
  Instruction *threadsPerParallelStageRoundUp   = BinaryOperator::Create(Instruction::Add, threadsPerParallelStageRoundDown, remainder, "threads.per.parallel.stage.round.up");
  S << numParallelThreads
    << threadsPerParallelStageRoundDown
    << remainder
    << threadsPerParallelStageRoundUp;

  // TODO: replace loads from/stores to this structure with
  // API calls; allow the runtime to implement private semantics
  // as necessary.

  // Load the initial value of each loop-caried register
  // from the liveouts structure.  This means that, upon
  // recovery, they acquire the value from /after/ the last
  // committed iteration.
  // This /also/ means that the liveout structure must
  // have private semantics.
  std::map<PHINode*,Value*> initialValues;
  for(unsigned i=0; i<phis.size(); ++i)
  {
    PHINode *phi = phis[i];

    Value *indices[] = { zero, ConstantInt::get(u32, N+i) };
    GetElementPtrInst *gep = GetElementPtrInst::Create(cast<PointerType>(object->getType()->getScalarType())->getElementType(),
                                object, ArrayRef<Value*>(&indices[0], &indices[2]) );
    LoadInst *load = new LoadInst( gep );
    load->setName("initial:" + phi->getName() );

    S << gep << load;

    initialValues[ phi ] = load;
  }

  Instruction *wid = CallInst::Create( api.getWorkerId() );
  wid->setName("wid");
  InstInsertPt::Beginning(invokeChain) << wid;

  Value *begin = zero;
  bool firstParallelStage = true;
  for(unsigned i=0; i<M; ++i)
  {
    BasicBlock *invoke = BasicBlock::Create(ctx, "invoke.stage." + Twine(i), dispatch_function);
    invoke->moveAfter( invokeChain );

    // Add live-ins
    SmallVector<Value*,5> args;
    for(VSet::iterator j=liveIns.begin(), z=liveIns.end(); j!=z; ++j)
      args.push_back(*j);
    for(BasicBlock::iterator j=header->begin(), z=header->end(); j!=z; ++j)
    {
      PHINode *phi = dyn_cast< PHINode >( &*j );
      if( !phi )
        break;
      const int idx = phi->getBasicBlockIndex( should_invoc_bb );
      if( idx != -1 )
      {
        Value *arg = initialValues[phi];
        if( !arg )
        {
          errs() << "For PHI " << *phi << '\n';
          assert( arg && "Null phi-initial value?!");
        }
        args.push_back( arg );
      }
    }
    for(unsigned from=0; from<i; ++from)
      if( queues.count( PreparedStrategy::StagePair(from,i) ) )
      {
        Value *arg = queueObjects[ PreparedStrategy::StagePair(from,i) ];
        assert( arg && "Null in-queue object?!");
        args.push_back( arg );
      }
    for(unsigned to=i+1; to<M; ++to)
      if( queues.count( PreparedStrategy::StagePair(i,to) ) )
      {
        Value *arg = queueObjects[ PreparedStrategy::StagePair(i,to) ];
        assert( arg && "Null out-queue object!?");
        args.push_back( arg );
      }

    Value *numThreads = one;
    if( stages[i].type == PipelineStage::Parallel )
    {
      if( firstParallelStage )
        numThreads = threadsPerParallelStageRoundUp;
      else
        numThreads = threadsPerParallelStageRoundDown;

      Instruction *repId = BinaryOperator::Create(Instruction::Sub, wid, begin, "rep.id", invoke);
      assert( repId && "Null rep-id?!");
      args.push_back( repId );
      assert( repId && "Null numThreads?!");
      args.push_back( numThreads );

      firstParallelStage = false;

      Constant *setRepId = api.getSetPstageReplica();
      SmallVector<Value *,1> onearg;
      onearg.push_back( repId );
      CallInst::Create(setRepId, ArrayRef<Value*>(onearg), "", invoke);
    }

    CallInst::Create(functions[i], ArrayRef<Value*>(args), "", invoke);
    new UnreachableInst(ctx, invoke);

    if( i == M-1 )
    {
      // Last one: don't compare, branch directly.
      BranchInst::Create(invoke, invokeChain);
      break;
    }

    BasicBlock *nextTest = BasicBlock::Create(ctx, "invoke.chain.", dispatch_function);
    nextTest->moveAfter(invokeChain);

    Instruction *end = BinaryOperator::Create(Instruction::Add, begin, numThreads, "end.", invokeChain);
    Instruction *cmp = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_ULT, wid, end, "is.stage." + Twine(i) + "?", invokeChain);
    BranchInst::Create(invoke, nextTest, cmp, invokeChain);

    begin = end;
    invokeChain = nextTest;
  }

  // Update the initial value of all PHIs
  for(std::map<PHINode*,Value*>::iterator i=initialValues.begin(), e=initialValues.end(); i!=e; ++i)
  {
    PHINode *phi = i->first;
    Value *init = i->second;

    for(unsigned pn=0, N=phi->getNumIncomingValues(); pn<N; ++pn)
      if( phi->getIncomingBlock(pn) == spawn_workers_bb )
        phi->setIncomingValue(pn, init);
  }


  // In the parent bb, call join, and then branch to
  // either the recovery block, or to the done block.
  Instruction *calljoin = CallInst::Create(join);
  cmp = new ICmpInst(ICmpInst::ICMP_EQ, calljoin, zero);
  br = BranchInst::Create(perform_recovery_bb, end_invoc_bb, cmp);
  InstInsertPt::End(join_workers_bb)
    << calljoin
    << cmp
    << br;

  // The recover basic block calls the recovery function
  // for sequential re-execution.
  // Steps:
  //  [low,high] iteration range.
  //  call recovery function.
  //  continue or break.
  Constant *lastCommitted = api.getLastCommitted(),
           *misspecIter   = api.getMisspecIter();
  Instruction *lastCommitIter = CallInst::Create(lastCommitted),
              *low = BinaryOperator::CreateNSWAdd(lastCommitIter,one),
              *high = CallInst::Create(misspecIter);

  low->setName("low_iter");
  high->setName("high_iter");

  InstInsertPt recover = InstInsertPt::End(perform_recovery_bb);
  recover
    << lastCommitIter
    << low
    << high;

  Function *recoveryFcn = recovery.fcn;

  std::vector<Value*> actuals;
  actuals.push_back(low);
  actuals.push_back(high);
  if( recovery.liveoutStructure.object )
  {
    const unsigned N = recovery.liveoutStructure.liveouts.size();
    const LiveoutStructure::PhiList &phis = recovery.liveoutStructure.phis;
    for(unsigned i=0; i<phis.size(); ++i)
    {
      Value *indices[] = { zero, ConstantInt::get(u32, N+i) };
      GetElementPtrInst *gep = GetElementPtrInst::Create(cast<PointerType>(
                recovery.liveoutStructure.object->getType()->getScalarType())->getElementType(),
                recovery.liveoutStructure.object, ArrayRef<Value*>(&indices[0], &indices[2]) );
      LoadInst *load = new LoadInst(gep);

      recover << gep << load;
      actuals.push_back(load);
    }
  }
  actuals.insert( actuals.end(),
    recovery.liveins.begin(), recovery.liveins.end() );
  Instruction *callRecover = CallInst::Create(recoveryFcn, ArrayRef<Value*>(actuals) );

  Constant *recoverDone = api.getRecoverDone();
  actuals.clear();
  actuals.push_back( callRecover );
  Instruction *callRecoverDone = CallInst::Create(recoverDone, ArrayRef<Value*>(actuals) );

  cmp = new ICmpInst(ICmpInst::ICMP_EQ, callRecover, zero);
  br = BranchInst::Create(restart_parallel_bb, end_invoc_bb, cmp);

  recover
    << callRecover
    << callRecoverDone
    << cmp
    << br;

  // Reset queues before restarting parallel region.
  restart_parallel_bb->moveAfter( perform_recovery_bb );
  InstInsertPt restart = InstInsertPt::Beginning(restart_parallel_bb);

  Constant *resetQueue = api.getResetQueue();
  for(QueueObjects::iterator i=queueObjects.begin(), e=queueObjects.end(); i!=e; ++i)
    restart << CallInst::Create(resetQueue, i->second);

  br = BranchInst::Create(spawn_workers_bb);
  restart
    << br;

  // This block executes when the loop has
  // successfully completed.
  Instruction *callend = CallInst::Create(endinvoc);

  RecoveryFunction::CtrlEdgeNumbers::const_iterator i = recovery.exitNumbers.begin(),
                                                    e = recovery.exitNumbers.end();
  assert( i != e && "No exits");

  BasicBlock *unreachable_bb = BasicBlock::Create(ctx, "error.exit.loop", fcn);
  SwitchInst *sw = SwitchInst::Create(callend, unreachable_bb, 0);
  for(;i!=e; ++i)
  {
    const RecoveryFunction::CtrlEdge &edge = i->first;
    unsigned edgeNumber = i->second;

    ConstantInt *guard = ConstantInt::get(u32, edgeNumber);

    RecoveryFunction::CtrlEdgeDestinations::const_iterator fnd =  recovery.exitDests.find(edge);
    assert( fnd != recovery.exitDests.end() && "Not in map");
    BasicBlock *dest = fnd->second;

    sw->addCase(guard, dest);
  }

  InstInsertPt shutdown = InstInsertPt::End(end_invoc_bb);
  Constant *freeQueue = api.getFreeQueue();
  for(QueueObjects::iterator i=queueObjects.begin(), e=queueObjects.end(); i!=e; ++i)
    shutdown << CallInst::Create(freeQueue, i->second);
  shutdown
    << CallInst::Create(api.getCleanupStrategy());
  shutdown
    << callend
    << sw;

  unreachable_bb->moveAfter(end_invoc_bb);
  InstInsertPt::End(unreachable_bb) << new UnreachableInst(ctx);

  // Finally, now that we moved the invoke chain out of the
  // main function and into a separate dispatch function,
  // we have broken the IR.
  // Stitch those back together by means of a live-in structure.
  // (1) Collect list of values used in the dispatch function, but which
  // are not available in the dispatch function.
  typedef std::set<Value*> VSet;
  VSet liveins;
  for(Function::iterator i=dispatch_function->begin(), e=dispatch_function->end(); i!=e; ++i)
  {
    BasicBlock *bb = &*i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      for(Instruction::op_iterator k=inst->op_begin(), Z=inst->op_end(); k!=Z; ++k)
      {
        Value *operand = &**k;
        if( Argument *arg = dyn_cast< Argument >(operand) )
        {
          if( arg->getParent() != dispatch_function )
            liveins.insert(arg);
        }
        else if( Instruction *iop = dyn_cast< Instruction >(operand) )
        {
          if( iop->getParent()->getParent() != dispatch_function )
            liveins.insert(iop);
        }
      }
    }
  }

  // Create a live-in structure type
  std::vector<Type*> fields;
  for(VSet::iterator i=liveins.begin(), e=liveins.end(); i!=e; ++i)
    fields.push_back( (*i)->getType() );
  StructType *liveinty = StructType::get(ctx, fields);
  AllocaInst *liveinObject = new AllocaInst(liveinty, 0, "liveins.to." + header->getName());
  InstInsertPt::Beginning(fcn) << liveinObject;

  // cast the argument to the right type
  Instruction *liveinObjectArg = new BitCastInst( &*dispatch_function->arg_begin(), liveinObject->getType());
  liveinObjectArg->setName("live.in.values");
  InstInsertPt restoreLiveIns = InstInsertPt::Beginning(dispatch_function);
  restoreLiveIns << liveinObjectArg;


  // spill/unspill
  unsigned index = 0;
  for(VSet::iterator i=liveins.begin(), e=liveins.end(); i!=e; ++i, ++index)
  {
    Value *outside = *i;
    storeIntoStructure(saveLiveIns, outside, liveinObject, index);
    Value *local = loadFromStructure(restoreLiveIns, liveinObjectArg, index);
    if( outside->hasName() )
      local->setName( outside->getName() );
    replaceUsesWithinFcn(outside, dispatch_function, local);
  }

  // Set this object as a parameter to
  Instruction *castLivein = new BitCastInst(liveinObject, voidptr);
  saveLiveIns << castLivein;
  callspawn->setArgOperand(2, castLivein);
}

}
}
