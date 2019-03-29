#define DEBUG_TYPE "doall-transform"

#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "liberty/CodeGen/DOALLTransform.h"

namespace liberty {
using namespace llvm;

void DOALLTransform::adjustForSpecDOALL(LoopDependenceInfo *LDI) {

  // worker spawning for speculation (process-based)
  specDOALLInvocation(LDI);

  // mark loop iteration boundaries
  // count one chunk as one iteration
  markIterationBoundaries();
}

void DOALLTransform::specDOALLInvocation(LoopDependenceInfo *LDI) {

  Loop *loop = LDI->loop;
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();
  LLVMContext &ctx = mod->getContext();
  Api api(mod);

  // get existing basic blocks
  BasicBlock *spawn_workers_bb = doall->getParLoopEntryPoint();
  BasicBlock *end_invoc_bb = doall->getParLoopExitPoint();

  // get runtime api calls
  Constant *begininvoc = api.getBeginInvocation(),
           *currentIter = api.getCurrentIter(),
           *spawn = api.getSpawn(),
           *join = api.getJoin(),
           *endinvoc = api.getEndInvocation();

  // setup for worker spawning
  //insert process-based dispatcher
  Instruction *callinvoc = CallInst::Create(begininvoc, "numThreads");
  Instruction *calliter  = CallInst::Create(currentIter, "current.iter");
  Instruction *numCores = new ZExtInst(callinvoc, int64);
  auto envPtr = doall->getEnvArray();
  Instruction *envCasted = new BitCastInst(envPtr, voidptr);
  auto chunkSize = ConstantInt::get(int64, LDI->DOALLChunkSize);
  InstInsertPt::Beginning(spawn_workers_bb) << callinvoc
                                            << calliter
                                            << numCores
                                            << envCasted;
  SmallVector<Value*,5> args;
  args.push_back(calliter);
  args.push_back((Value*)task->F);
  args.push_back(envCasted);
  args.push_back(numCores);
  args.push_back(chunkSize);

  /*
  for (auto arg: args) {
    errs() << "arg->getType() " << *arg->getType() << '\n';
  }
  FunctionType *FTy = cast<FunctionType>(cast<PointerType>(spawn->getType())->getElementType());
 // FunctionType *ftSpawnTy = spawn->getType();
  for (unsigned i = 0; i != FTy->getNumParams(); ++i) {
    errs() << "FTy->getParamType(i) " << *FTy->getParamType(i) << '\n';
  }
  */

  CallInst *callspawn = CallInst::Create(spawn, ArrayRef<Value*>(args));

    // remove non-spec dispatcher
  Instruction *nonSpecDispatcher = nullptr;
  for (Instruction &I : *spawn_workers_bb) {
    if (CallInst *cI = dyn_cast<CallInst>(&I)) {
      Function *cF = cI->getCalledFunction();
      if (cF && cF == doall->getTaskDispatcher()) {
        nonSpecDispatcher = &I;
        break;
      }
    }
  }
  assert(nonSpecDispatcher && "Cannot find non-spec dispatcher");

  InstInsertPt::Before(nonSpecDispatcher) << callspawn;
  //InstInsertPt::End(spawn_workers_bb) << callspawn;

  nonSpecDispatcher->eraseFromParent();

  // post-parallel execution handling
  // Insert join_workers basic block: waits for all workers to finish and
  //                                  checks if misspec occured.
  //                                  Branches to either end_invoc_bb or
  //                                  perform_recovery (in case of misspec)

  //BasicBlock *join_workers_bb = BasicBlock::Create(ctx, "join_workers", fcn);
  //join_workers_bb->moveAfter(spawn_workers_bb);
  Instruction *calljoin = CallInst::Create(join);
  InstInsertPt::After(callspawn) << calljoin;
  ConstantInt *zero = ConstantInt::get(int32, 0);
  Instruction *cmp = new ICmpInst(ICmpInst::ICMP_EQ, calljoin, zero);
  InstInsertPt::End(spawn_workers_bb) << cmp;
  BasicBlock *perform_recovery_bb =
      BasicBlock::Create(ctx, "perform_recovery", fcn);
  Instruction *br = BranchInst::Create(perform_recovery_bb, end_invoc_bb, cmp);
  //spawn_workers_bb->getTerminator()->replaceUsesOfWith(end_invoc_bb,
  //                                                     join_workers_bb);
  BasicBlock::iterator ii(spawn_workers_bb->getTerminator());
  ReplaceInstWithInst(spawn_workers_bb->getInstList(), ii, br);

  // call end_invocation in the loop exit block
  Instruction *callend = CallInst::Create(endinvoc);
  InstInsertPt::End(end_invoc_bb) << callend;

  // Recovery not supported for now. Abort in case of misspec
  FunctionType *abortFTy = FunctionType::get(voidty, false);
  Constant *abortF = mod->getOrInsertFunction("abort", abortFTy);
  Instruction *callabort = CallInst::Create(abortF);
  Instruction *unreachableinst = new UnreachableInst(ctx);
  InstInsertPt::End(perform_recovery_bb) << callabort
                                         << unreachableinst;
}

void DOALLTransform::markIterationBoundaries() {

  Function *fcn = task->F;
  BasicBlock *preheader = &fcn->getEntryBlock();
  Api api(mod);
  LLVMContext &ctx = mod->getContext();
  BasicBlock *header = preheader->getTerminator()->getSuccessor(0);
  Constant *beginiter = api.getBeginIter();
  Constant *enditer = api.getEndIter();
  Constant *workerfinishes = api.getWorkerFinishes();
  Constant *finalIterCkptCheck = api.getFinalIterCkptCheck();
  ConstantInt *one = ConstantInt::get(int32, 1);

  // Call begin iter at top of loop
  CallInst::Create( beginiter, "", &*( header->getFirstInsertionPt() ) );

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  LoopInfo &li = mloops.getAnalysis_LoopInfo( fcn );
  Loop *loop = li.getLoopFor(header);

  // Identify the edges at the end of an iteration
  // == loop backedges, loop exits.
  typedef std::pair<TerminatorInst *, unsigned> CtrlEdge;
  typedef std::vector< CtrlEdge > CtrlEdgesV;
  typedef std::set< CtrlEdge > CtrlEdgesS;
  CtrlEdgesV iterationBounds;
  CtrlEdgesS loopExits;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();
    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      BasicBlock *dest = term->getSuccessor(sn);

      // Loop back edge
      if( dest == header )
        iterationBounds.push_back(std::make_pair(term, sn));

      // Loop exit
      else if( ! loop->contains(dest) ) {
        auto edge = std::make_pair(term,sn);
        iterationBounds.push_back(edge);
        loopExits.insert(edge);
      }
    }
  }

  for(unsigned i=0, N=iterationBounds.size(); i<N; ++i)
  {
    TerminatorInst *term = iterationBounds[i].first;
    BasicBlock *source = term->getParent();
    unsigned sn = iterationBounds[i].second;
    BasicBlock *dest = term->getSuccessor(sn);

    // if loop exit, noelle inserts exit block to store live-outs
    // insert end.iter after this block
    if (loopExits.count(iterationBounds[i])) {
      source = term->getSuccessor(sn);
      term = source->getTerminator();
      sn = 0;
      dest = term->getSuccessor(0);
      assert(isa<ReturnInst>(dest->getTerminator()) &&
             "Successor of exit block in parallelized loop should include a "
             "return statement");
    }

    {
      BasicBlock *split = BasicBlock::Create(ctx,"end.iter",fcn);

      // Update PHIs in dest
      for(BasicBlock::iterator j=dest->begin(), z=dest->end(); j!=z; ++j)
      {
        PHINode *phi = dyn_cast< PHINode >( &*j );
        if( !phi )
          break;

        int idx = phi->getBasicBlockIndex(source);
        if( idx != -1 )
          phi->setIncomingBlock(idx,split);
      }
      term->setSuccessor(sn, split);
      split->moveAfter( source );

      CallInst::Create(enditer, "", split);

      // Call _worker_finishes() to end this worker
      // Should also announce which loop exit was taken
      // Return 1 to indicate no misspec for now
      // TODO: in case of multiple loop exits, pass proper exittoken to
      // worker_finishes and add switch statement at end_invoc_bb
      if (loopExits.count(iterationBounds[i])) {
        // check if current worker executes one less iteration than some other.
        // If so, call runtime function to insert a begin_iter and end_iter
        // calls to trigger checkpoint if needed
        IRBuilder<> endIterBuilder(split);

        //auto numCoresOffset = endIterBuilder.CreateZExtOrTrunc(
       //     endIterBuilder.CreateMul(task->numCoresArg, task->chunkSizeArg),
        //    task->outermostLoopIV->getType());

        auto numCoresOffset =
            endIterBuilder.CreateMul(task->numCoresArg, task->chunkSizeArg);

        auto remIter = endIterBuilder.CreateURem(
            endIterBuilder.CreateZExt(task->clonedIVBounds.cmpIVTo, int64),
            numCoresOffset);

        /*
            auto outerIVCmp = outerHBuilder.CreateICmpEQ(
    task->outermostLoopIV,
    task->clonedIVBounds.cmpIVTo
      );
      outerHBuilder.CreateCondBr(outerIVCmp, innerHeader, task->loop
      ExitBlocks[0]);
      */

        SmallVector<Value *, 2> args;
        args.push_back(remIter);
        args.push_back(task->chunkSizeArg);

        CallInst::Create(finalIterCkptCheck, ArrayRef<Value *>(args), "",
                         split);

        CallInst::Create(workerfinishes, one, "", split);
      }

      BranchInst::Create(dest,split);
    }
  }

  mloops.forget(fcn);
}

} // namespace liberty
