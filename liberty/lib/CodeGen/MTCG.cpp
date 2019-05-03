#define DEBUG_TYPE "mtcg"

#include "llvm/ADT/Statistic.h"

#include "liberty/CodeGen/MTCG.h"

#include "liberty/Utilities/ModuleLoops.h"

#include "liberty/CodeGen/PrintStage.h"

#if (MTCG_CTRL_DEBUG || MTCG_VALUE_DEBUG)
#include "liberty/Utilities/InsertPrintf.h"
#endif

#if MTCG_VALUE_DEBUG
#include "llvm/IR/InstIterator.h"
#include "Metadata.h"
#include <sstream>
#endif

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numLoops, "Loops parallelized");
STATISTIC(numOffIters, "Off-iterations created");
STATISTIC(numStagesCreated, "Stages created");
STATISTIC(numProduce, "Produces inserted (normal)");
STATISTIC(numProduceReplicated, "Produces inserted (to a replicated stage)");
STATISTIC(numConsume, "Consumes inserted (normal)");
STATISTIC(numConsumeReplicated, "Consumes inserted (in a replicated stage)");

cl::opt<bool> WriteStageCFGs(
  "mtcg-write-cfgs", cl::init(false), cl::Hidden,
  cl::desc("Write stage CFGs to DOT (SLOW)"));


void MTCG::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< ModuleLoops >();
  au.addRequired< Selector >();
  au.addRequired< Preprocess >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
}

bool MTCG::runOnModule(Module &module)
{
  bool modified = false;
  const Selector &selector = getAnalysis< Selector >();
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();

  // Study each of the strategies, and then create the stage functions
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    BasicBlock *header = i->first;
    LoopInfo &li = mloops.getAnalysis_LoopInfo( header->getParent() );
    Loop *loop = li.getLoopFor(header);

    if( PipelineStrategy *ps = dyn_cast<PipelineStrategy>(&*(i->second)) )
    {
      // PHASE 1 ----------- PLANNING
      elaboratedStrategies.push_back( PreparedStrategy(loop,ps) );
      PreparedStrategy &strategy = elaboratedStrategies.back();

      if( WriteStageCFGs )
        for(unsigned stageno=0, N=strategy.numStages(); stageno<N; ++stageno)
          writeStageCFG(strategy.loop, stageno, "on",
            strategy.relevant[stageno], strategy.instructions[stageno],
            strategy.produces[stageno], strategy.consumes[stageno]);

      // PHASE 2 ------------ CREATE STAGES
      if( runOnStrategy(strategy) )
      {
        mloops.forget( header->getParent() );
        modified = true;
      }
    }
  }

  // PHASE 3 ------------- CREATE AN INVOCATION

  for(unsigned i=0, N=elaboratedStrategies.size(); i<N; ++i)
  {
    BasicBlock *header = elaboratedStrategies[i].lps->getHeader();
    LoopInfo &li = mloops.getAnalysis_LoopInfo( header->getParent() );
    elaboratedStrategies[i].loop = li.getLoopFor(header);

    createParallelInvocation( elaboratedStrategies[i] );
    modified = true;
  }

  return modified;
}

bool MTCG::runOnStrategy(PreparedStrategy &strategy)
{
  DEBUG(errs() << "-------------- Strategy "
               << strategy.loop->getHeader()->getName()
               << ' ' << *strategy.lps << " -------------\n");
  // PHASE 2 ------------- CREATE STAGES

  // Grab control speculation, if available
  NoControlSpeculation nocs;
  ControlSpeculation *cs = &nocs;
  if( ProfileGuidedControlSpeculator *pgcs = getAnalysisIfAvailable< ProfileGuidedControlSpeculator >() )
    cs = pgcs;

  cs->setLoopOfInterest( strategy.loop->getHeader() );

  // Determine loop-post-dominators
  //  (post-dominators of the control-flow-graph if
  //   everything before the loop is coalesced into a single node,
  //   and everything after the loop is coalesced into another single node).
  LoopDom dt(*cs, strategy.loop);
  LoopPostDom pdt(*cs, strategy.loop);

  for(unsigned stageno=0, N=strategy.numStages(); stageno<N; ++stageno)
    strategy.functions[stageno] = createStage(strategy, stageno, dt, pdt);

  ++numLoops;
  return true;
}

Function *MTCG::createStage(PreparedStrategy &strategy, unsigned stageno, const LoopDom &dt, const LoopPostDom &pdt)
{
  DEBUG(errs() << "-------------- Transform Stage " << stageno << " -------------\n");
  Loop *loop = strategy.loop;
  const PipelineStrategy::Stages &stages = strategy.lps->stages;
  const PipelineStage &stage = stages[stageno];
  const PipelineStrategy::CrossStageDependences &xdeps = strategy.lps->crossStageDeps;
  const unsigned N = stages.size();

  ++numStagesCreated;

  // Maintain a correspondence between original and new blocks/args/instructions/liveins etc.
  VMap vmap_on, vmap_off;
  Stage2Value stage2queue(N);

  Value *repId=0, *repFactor=0;
  Function *fcn = createFunction(
    loop,stage,stageno,N,strategy.liveIns,strategy.queues,
    stage2queue,vmap_on,
    &repId, &repFactor);
  vmap_off = vmap_on;

  BasicBlock *preheader_on = createOnIteration(
    strategy,stageno,stage2queue,fcn,vmap_on,dt,pdt);

  BasicBlock *preheader = 0;
  if( stage.type == PipelineStage::Sequential )
    preheader = preheader_on;

  else if( stage.type == PipelineStage::Parallel )
  {
    BasicBlock *preheader_off = createOffIteration(
      loop,stages,strategy.liveIns,strategy.available,xdeps,stageno,stage2queue,fcn,vmap_off,dt,pdt);

    preheader = stitchLoops(
      loop,
      vmap_on,preheader_on,
      vmap_off,preheader_off,
      strategy.liveIns,
      repId,repFactor);
  }

  // Entry branches to loop header
  BasicBlock *entry = &fcn->getEntryBlock();
  BranchInst::Create( preheader, entry );

  markIterationBoundaries(preheader);

#if (MTCG_CTRL_DEBUG || MTCG_VALUE_DEBUG)
  Module *mod = fcn->getParent();
  Api api(mod);
  Constant* debugprintf = api.getDebugPrintf();
#endif

#if MTCG_CTRL_DEBUG
  for (Function::iterator bi = fcn->begin() ; bi != fcn->end() ; bi++)
  {
    Constant* name = getStringLiteralExpression( *mod, (*bi).getName().str()+"\n" );
    Value* args[] = { name };
    CallInst::Create(debugprintf, args, "debug-ctrl", (*bi).getFirstNonPHI());
  }
#endif

#if MTCG_VALUE_DEBUG
  for (inst_iterator ii = inst_begin(fcn) ; ii != inst_end(fcn) ; ii++)
  {
    Instruction* inst = &*ii;

    int instr_id = Namer::getInstrId(inst);
    if ( instr_id == -1 )
      continue;

    const char* formatstr = getFormatStringForType(inst->getType());
    if (!formatstr)
      continue;

    std::ostringstream ss;
    ss << instr_id;

    std::string str = "  * id " + ss.str() + " value " + std::string(formatstr) + "\n";
    Constant*   arg = getStringLiteralExpression( *mod, str );
    Value*      args[] = { arg , inst };

    CallInst* ci = CallInst::Create(debugprintf, args, "debug-value");
    ci->insertBefore( inst->getParent()->getTerminator() );
  }
#endif

  return fcn;
}

static BasicBlock *getUniqueSuccessor(BasicBlock *pred)
{
  TerminatorInst *term = pred->getTerminator();
  const unsigned N=term->getNumSuccessors();
  assert( N > 0 && "Cannot get unique successor: No successors");
  BasicBlock *succ = term->getSuccessor(0);
  for(unsigned i=1; i<N; ++i)
    assert( term->getSuccessor(i) == succ && "Block does not have a unique successor");
  return succ;
}

BasicBlock *MTCG::stitchLoops(
  Loop *loop, // original function
  const VMap &vmap_on, BasicBlock *preheader_on, // First version
  const VMap &vmap_off, BasicBlock *preheader_off, // Second version
  const VSet &liveIns, // loop live-ins
  Value *repId, Value *repFactor // which worker? how many workers?
  ) const
{
  BasicBlock *header = loop->getHeader();
  Function *fcn = preheader_on->getParent();
  Module *mod = fcn->getParent();
  Api api(mod);
  LLVMContext &ctx = mod->getContext();

  BasicBlock *newPreheader = BasicBlock::Create(ctx, "stitch.preheader." + header->getName(), fcn);
  newPreheader->moveAfter( &fcn->getEntryBlock() );
  BasicBlock *newHeader = BasicBlock::Create(ctx, "stitch.header." + header->getName(), fcn);
  BranchInst::Create(newHeader, newPreheader);
  newHeader->moveAfter(newPreheader);

  BasicBlock *header_on = getUniqueSuccessor( preheader_on ),
             *header_off = getUniqueSuccessor( preheader_off );

  replaceIncomingEdgesExcept(header_on,preheader_on,newHeader);

  // For each PHI node which appears in both the ON and OFF versions of this stage.
  for(BasicBlock::iterator i=header->begin(), e=header->end(); i!=e; ++i)
  {
    PHINode *phi = dyn_cast<PHINode>( &*i );
    if( !phi )
      break;

    // Find the corresponding phi nodes on the ON/OFF versions.
    PHINode *phi_on  = 0, *phi_off = 0;
    VMap::const_iterator fnd = vmap_on.find(phi);
    if( fnd != vmap_on.end() )
      phi_on = dyn_cast<PHINode>( fnd->second );
    fnd = vmap_off.find(phi);
    if( fnd != vmap_off.end() )
      phi_off = dyn_cast<PHINode>( fnd->second );

    if( !phi_on && !phi_off )
      continue;

    if( !phi_off )
    {
      // Create a dummy PHI whose incoming values are all 'undef'
      phi_off = PHINode::Create( phi_on->getType(), 0, "phi_off.undef", &*(header_off->getFirstInsertionPt()) );

      Value* incoming_v = NULL;

      for (unsigned i = 0 ; i < phi->getNumIncomingValues() ; i++)
      {
        if ( i == 0 )
          incoming_v = phi->getIncomingValue(i);
        else if (incoming_v != phi->getIncomingValue(i))
          incoming_v = NULL;
      }

      if ( incoming_v == NULL || !liveIns.count(incoming_v) )
        incoming_v = UndefValue::get( phi->getType() );
      else
      {
        // all incoming values are idential, and live-ins, thus it is okay to take any operand from
        // phi_on instruction as an incoming value of phi_off

        incoming_v = phi_on->getIncomingValue(0);
      }

      for(pred_iterator i=pred_begin(header_off), e=pred_end(header_off); i!=e; ++i)
      {
        BasicBlock *pred = *i;
        phi_off->addIncoming(incoming_v, pred);
      }
    }

    if( !phi_on || !phi_off )
    {
      // This is an error.

      errs() << "PHI Node: " << *phi << '\n';
      if( phi_on )
        errs() << " + Appears in the ON iteration as: " << *phi_on << '\n';
      else
        errs() << " - Does NOT appear in the ON iteration.\n";

      if( phi_off )
        errs() << " + Appears in the OFF iteration as: " << *phi_off << '\n';
      else
        errs() << " - Does NOT appear in the OFF iteration.\n";

      assert( phi_on && phi_off
      && "PHI node should appear in neither ON nor OFF, or both ON and OFF.");
    }


    PHINode *newPhi = PHINode::Create(
      phi->getType(),
      4,
      "", newHeader);

    if( phi_on )
      stitchPhi(preheader_on, phi_on, newPreheader, newPhi);

    if( phi_off )
      stitchPhi(preheader_off, phi_off, newPreheader, newPhi);
  }

  replaceIncomingEdgesExcept(header_off,preheader_off,newHeader);

  // Alternate between the ON and OFF iterations.
  Constant *getIterNum = api.getCurrentIter();
  Value *iter = CallInst::Create(getIterNum, "current.iteration", newHeader);
  Value *phase  = BinaryOperator::Create(Instruction::URem, iter, repFactor, "phase", newHeader);
  Value *cmp  = CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_EQ, phase, repId, "on/off", newHeader);
  BranchInst::Create(preheader_on,preheader_off,cmp, newHeader);

  preheader_on->setName( "ON." + preheader_on->getName() );
  preheader_off->setName( "OFF." + preheader_off->getName() );

  return newPreheader;
}

void MTCG::replaceIncomingEdgesExcept(BasicBlock *oldTarget, BasicBlock *excludePred, BasicBlock *newTarget) const
{
  for(Value::user_iterator i=oldTarget->user_begin(), e=oldTarget->user_end(); i!=e; ++i)
  {
    TerminatorInst *term = dyn_cast< TerminatorInst >( &**i );
    if( term && term->getParent() != excludePred )
      term->replaceUsesOfWith(oldTarget, newTarget);
  }
}

void MTCG::stitchPhi(
  BasicBlock *oldPreheader, PHINode *oldPhi,
  BasicBlock *newPreheader, PHINode *newPhi) const
{
  newPhi->setName( "stitch." + oldPhi->getName() );

  const unsigned N=oldPhi->getNumIncomingValues();
  BBVec incoming(N);
  for(unsigned i=0; i<N; ++i)
    incoming[i] = oldPhi->getIncomingBlock(i);

  for(unsigned i=0; i<N; ++i)
  {
    BasicBlock *in = incoming[i];
    if( in == oldPreheader )
      in = newPreheader;

    if( newPhi->getBasicBlockIndex(in) == -1 )
      newPhi->addIncoming( oldPhi->getIncomingValue(i), in );
  }

  oldPhi->setIncomingValue(0, newPhi);
  oldPhi->setIncomingBlock(0, oldPreheader);
  while( oldPhi->getNumIncomingValues() > 1 )
    oldPhi->removeIncomingValue(1);
}

BasicBlock *MTCG::createOnIteration(
  PreparedStrategy &strategy, unsigned stageno, const Stage2Value &stage2queue, Function *fcn, VMap &vmap,
  const LoopDom &dt, const LoopPostDom &pdt)
{
  Loop *loop = strategy.loop;
  const ISet &insts = strategy.instructions[stageno];
  const VSet &liveIns = strategy.liveIns;
  const BBSet &rel = strategy.relevant[stageno];
  const PreparedStrategy::ConsumeFrom &cons = strategy.consumes[stageno];
  const PreparedStrategy::ProduceTo &prods = strategy.produces[stageno];

  DEBUG(errs() << "Stage has "
               << insts.size() << " instructions; "
               << rel.size() << " basic blocks; consumes "
               << cons.size() << " values\n");

  Twine blockNameSuffix = Twine();// = "(on-" + Twine(stageno) + ")";

  return copyInstructions(
    loop,stageno,
    insts,liveIns,cons,prods,stage2queue,rel,dt,pdt,
    fcn,vmap,
    blockNameSuffix);
}

BasicBlock *MTCG::createOffIteration(
  Loop *loop, const PipelineStrategy::Stages &stages,
  const VSet &liveIns, const PreparedStrategy::Stage2VSet &available,
  const PipelineStrategy::CrossStageDependences &xdeps, unsigned stageno,
  const Stage2Value &stage2queue, Function *fcn, VMap &vmap,
  const LoopDom &dt, const LoopPostDom &pdt)
{
  ++numOffIters;

  ISet off_insts;
  VSet off_avail;
  BBSet off_rel;
  PreparedStrategy::ConsumeFrom off_cons;
  PreparedStrategy::ProduceTo off_prods;

  PreparedStrategy::studyStage(
    stages,xdeps,
    liveIns, available,
    stageno,
    off_insts,off_avail,off_rel,off_cons);

/* Removing this unnecessary restriction.  Instead, we need a new variant of
 * the produce/consume operations which are specialized for the case where the
 * consume occurs in a replicable stage. -NPJ.

  assert( off_cons.empty()
  && "Replicated off-iterations should not consume");
*/
  DEBUG(errs() << "OFF-iteration of stage " << stageno << " has "
               << off_insts.size() << " instructions; "
               << off_rel.size() << " basic blocks; consumes "
               << off_cons.size() << " values\n");

  if( off_rel.empty() )
  {
    // Degenerate case: create an empty OFF iteration.
    LLVMContext &ctx = fcn->getContext();

    BasicBlock *degenerate_preheader = BasicBlock::Create(ctx, "degenerate.preheader", fcn);
    BasicBlock *degenerate_header    = BasicBlock::Create(ctx, "degenerate.header",    fcn);

    vmap.insert( std::make_pair(loop->getLoopPreheader(), degenerate_preheader) );
    vmap.insert( std::make_pair(loop->getHeader(), degenerate_header) );

    BranchInst::Create(degenerate_header, degenerate_preheader);
    BranchInst::Create(degenerate_header, degenerate_header);

    return degenerate_preheader;
  }

  if( WriteStageCFGs )
    writeStageCFG(loop, stageno, "off", off_rel, off_insts, off_prods, off_cons);

  Twine blockNameSuffix = Twine();// = "(off-" + Twine(stageno) + ")";
  return copyInstructions(
    loop,stageno,
    off_insts,liveIns,off_cons,off_prods,stage2queue,off_rel,dt,pdt,
    fcn,vmap,
    blockNameSuffix);
}

Function *MTCG::createFunction(
  Loop *loop, const PipelineStage &stage, unsigned stageno, unsigned N,
  const VSet &liveIns, const PreparedStrategy::StagePairs &queues,
  // Outputs
  Stage2Value &stage2queue, VMap &vmap,
  Value **repId, Value **repFactor)
{
  BasicBlock *header = loop->getHeader();
  Function *origFcn = header->getParent();
  Module *mod = origFcn->getParent();
  Api api(mod);
  LLVMContext &ctx = mod->getContext();

  // The function arguments are: live-ins, initial-values-for-phis, in-Qs, out-Qs, rep ID and rep factor.
  SmallVector<Type*,5> argTys;
  for(VSet::iterator i=liveIns.begin(), e=liveIns.end(); i!=e; ++i)
    argTys.push_back( (*i)->getType() );
  for(BasicBlock::iterator i=header->begin(), e=header->end(); i!=e; ++i)
  {
    PHINode *phi = dyn_cast< PHINode >(&*i);
    if( !phi )
      break;
    argTys.push_back( phi->getType() );
  }
  for(unsigned from=0; from<stageno; ++from)
    if( queues.count( PreparedStrategy::StagePair(from,stageno) ) )
      argTys.push_back( api.getQueueType() );
  for(unsigned to=stageno+1; to<N; ++to)
    if( queues.count( PreparedStrategy::StagePair(stageno,to) ) )
      argTys.push_back( api.getQueueType() );
  IntegerType *u32 = api.getU32();
  if( stage.type == PipelineStage::Parallel )
  {
    argTys.push_back( u32 );
    argTys.push_back( u32 );
  }

  FunctionType *fty = FunctionType::get( api.getVoid(), ArrayRef<Type*>(argTys), false);
  Twine name = "__specpriv_pipeline__" + origFcn->getName() + "__" + header->getName()
    + (stage.type == PipelineStage::Parallel ? "__p" : "__s") + Twine(stageno);
  Function *fcn = Function::Create(fty, GlobalValue::InternalLinkage, name, mod);
  fcn->setDoesNotReturn();

  // Name the function arguments according to the scheme above.
  Function::arg_iterator ai = fcn->arg_begin();
  for(VSet::iterator i=liveIns.begin(), e=liveIns.end(); i!=e; ++i)
  {
    ai->setName( "livein_" + (*i)->getName() );
    ++ai;
  }
  for(BasicBlock::iterator i=header->begin(), e=header->end(); i!=e; ++i)
  {
    PHINode *phi = dyn_cast< PHINode >(&*i);
    if( !phi )
      break;
    ai->setName( "initial_phi_" + phi->getName() );
    ++ai;
  }
  for(unsigned from=0; from<stageno; ++from)
    if( queues.count( PreparedStrategy::StagePair(from,stageno) ) )
    {
      Twine name = "q_from_" + Twine(from) + ".";
      ai->setName( name );
      stage2queue[from] = &*ai;
      ++ai;
    }
  for(unsigned to=stageno+1; to<N; ++to)
    if( queues.count( PreparedStrategy::StagePair(stageno,to) ) )
    {
      Twine name = "q_to_" + Twine(to) + ".";
      ai->setName( name );
      stage2queue[to] = &*ai;
      ++ai;
    }
  if( stage.type == PipelineStage::Parallel )
  {
    *repId = &*ai;
    (*repId)->setName( "pstage_replica_id" );
    ++ai;
    *repFactor = &*ai;
    (*repFactor)->setName( "pstage_replication_factor" );
  }

  // Populate vmap with live-in args
  ai = fcn->arg_begin();
  for(VSet::iterator i=liveIns.begin(), e=liveIns.end(); i!=e; ++i, ++ai)
    vmap[ *i ] = &*ai;

  BasicBlock::Create(ctx, "entry", fcn);
  return fcn;
}

// Compute a metadata node which we will add onto a produce/consume.
// This allows later passes to determine which produces feed which consumes.
MDNode *MTCG::prodConsCorrespondenceTag(
  Loop *loop, unsigned srcStage, unsigned dstStage,
  BasicBlock *block, unsigned posInBlock)
{
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();
  LLVMContext &ctx = fcn->getContext();
  IntegerType *u32 = Type::getInt32Ty(ctx);

  MDString    *fcnName    = MDString::get(ctx, fcn->getName());
  MDString    *headerName = MDString::get(ctx, header->getName());
  ConstantInt *src        = ConstantInt::get(u32, srcStage);
  ConstantInt *dst        = ConstantInt::get(u32, dstStage);
  MDString    *bbName     = MDString::get(ctx, block->getName());
  ConstantInt *pos        = ConstantInt::get(u32, posInBlock);

  //sot: Metadata changed. Not more part of the Value hierarchy
  //convert values to metadata with ValueAsMetadata::get
  //Value *values[] = { fcnName, headerName, src, dst, bbName, pos };
  Metadata *values[] = { fcnName, headerName, ValueAsMetadata::get(src),
                         ValueAsMetadata::get(dst), bbName,
                         ValueAsMetadata::get(pos) };
  //return MDNode::get(ctx, ArrayRef<Value*>(&values[0], &values[6]));
  return MDNode::get(ctx, ArrayRef<Metadata*>(&values[0], &values[6]));
}

BasicBlock *MTCG::copyInstructions(
  Loop *loop, unsigned stageno, const ISet &insts,
  const VSet &liveIns,
  const PreparedStrategy::ConsumeFrom &cons, const PreparedStrategy::ProduceTo &prods,
  const Stage2Value &stage2queue, const BBSet &rel,
  const LoopDom &dt, const LoopPostDom &pdt,
  // Outputs
  Function *fcn, VMap &vmap,
  // Optional
  Twine blockNameSuffix)
{
  BasicBlock *header = loop->getHeader();
  BasicBlock *loop_preheader = loop->getLoopPreheader();
  Module *mod = header->getParent()->getParent();
  Api api(mod);
  LLVMContext &ctx = mod->getContext();
  ControlSpeculation &cs = pdt.getControlSpeculation();

  BBVec newBlocks;
  Twine preheaderName = "preheader" + blockNameSuffix;
  BasicBlock *preheader = BasicBlock::Create(ctx, preheaderName, fcn);
  newBlocks.push_back(preheader);

  // To speed up calls to closestRelevantPostdom and closestRelevantDom.
  BB2LB cache_crpd, cache_crd;

  // For each relevant basic block in control-flow order
  // from loop header.
  const unsigned num_blocks_before = newBlocks.size();
  typedef std::list<BasicBlock*> BBList;
  BBSet visited;
  BBList fringe;
  fringe.push_back( header );
  while( !fringe.empty() )
  {
    BasicBlock *bb = fringe.front();
    fringe.pop_front();

    // Avoid cycles.
    if( visited.count(bb) )
      continue;
    visited.insert(bb);

    if( rel.count(bb) )
    {
      // Create new basic block.
      Twine blockName = bb->getName() + blockNameSuffix;
      BasicBlock *cloneBB = BasicBlock::Create(ctx, blockName, fcn);
      vmap[ bb ] = cloneBB;
      newBlocks.push_back( cloneBB );

      unsigned positionInBlock = 0;

      // Foreach instruction in this block:
      //  - possibly consume its value from an earlier stage.
      //  - possibly copy the instruction.
      //  - possibly produce its value to a later stage.
      for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i, ++positionInBlock)
      {
        Instruction *inst = &*i;

        // Do we need to consume its value from an earlier stage?
        PreparedStrategy::ConsumeFrom::const_iterator zz = cons.find(inst);
        if( zz != cons.end() )
        {
          const PreparedStrategy::ConsumeStartPoint &consumeFrom = zz->second;
          unsigned consumeFromStage = consumeFrom.first;
          bool     consumeWithinReplicable = consumeFrom.second;

          MDNode *tag = prodConsCorrespondenceTag(
            loop, consumeFromStage, stageno, bb, positionInBlock);

          Value *consumeFromQueue = stage2queue[ consumeFromStage ];
          Value *v = insertConsume(api, cloneBB, consumeFromQueue, inst->getType(), tag, consumeWithinReplicable);
          v->setName( inst->getName() );
          vmap[ inst ] = v;
        }

        // Should we copy this instruction to this thread?
        else if( insts.count(inst) )
        {
          Instruction *cloneInst = inst->clone();
          cloneInst->setName( inst->getName() );
          if( isa< PHINode >(cloneInst) )
            cloneBB->getInstList().push_front(cloneInst);
          else
            cloneBB->getInstList().push_back(cloneInst);
          vmap[ inst ] = cloneInst;

          // Should we produce its value to a later stage?
          PreparedStrategy::ProduceTo::const_iterator zz = prods.find(inst);
          if( zz != prods.end() )
          {
            const PreparedStrategy::ProduceEndPoints &endPoints = zz->second;
            for(PreparedStrategy::ProduceEndPoints::const_iterator j=endPoints.begin(), z=endPoints.end(); j!=z; ++j)
            {
              const PreparedStrategy::ProduceEndPoint &endPoint = *j;
              unsigned produceToStage = endPoint.first;
              bool     produceToReplicated = endPoint.second;

              MDNode *tag = prodConsCorrespondenceTag(
                loop, stageno, produceToStage, bb, positionInBlock);
              Value *produceToQueue = stage2queue[ produceToStage ];
              insertProduce(api, cloneBB, produceToQueue, inst, tag, produceToReplicated);
            }
          }
        }
      }

      //errs() << "BB:\n" << *bb << "\n     Clone:\n"  << *cloneBB << "\n";

      // If this block did not get a terminator,
      // we add an unconditional branch.
      if( cloneBB->empty() || ! isa<TerminatorInst>( cloneBB->back() ) )
      {
        TerminatorInst *oldTerm = bb->getTerminator();
//          assert( cs.isSpeculativelyUnconditional(oldTerm)
//          && "Blocks with default branch must have unconditional branch");

        // There must be a unique closest relevant post-dom
        BasicBlock *unique_dst = 0;
        for(unsigned sn=0, N=oldTerm->getNumSuccessors(); sn<N; ++sn)
        {
          if( cs.isSpeculativelyDead(oldTerm,sn) )
            continue;

          ControlSpeculation::LoopBlock crpd = closestRelevantPostdom(
            oldTerm->getSuccessor(sn), rel, pdt, cache_crpd);
          assert( crpd.isValid() && "closestRelevantPostdom invalid");
          if( 0 == unique_dst )
          {
            unique_dst = crpd.getBlock();

            if ( crpd.isLoopContinue() )
            {
              assert( rel.count(header) );
              unique_dst = dyn_cast<BasicBlock>( header );
            }
          }
          else if( crpd.getBlock() != unique_dst )
          {
            if ( !crpd.isLoopContinue() || unique_dst != header )
            {
              errs() << "CRPD " << crpd.getBlock()->getName() << " vs " << unique_dst->getName() << '\n';
              errs() << "oldTerm basic block " << oldTerm->getParent()->getName() << '\n';
              assert( false
                  && "Terminator must have a unique closest relevant postdom");
            }
          }
        }
        BranchInst::Create(unique_dst, cloneBB);
      }
    }

    // Next visit the basic blocks which follow 'bb'
    TerminatorInst *term = bb->getTerminator();
    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
      fringe.push_back( term->getSuccessor(sn) );
  }
  const unsigned numBlocksCopied = newBlocks.size() - num_blocks_before;

  // Special case: no relevant bbs
  // (this can happen when creating an OFF iteration with no
  // replicated instructions)
  if( numBlocksCopied < 1 )
  {
    Twine blockName = "no.replicated.instructions" + blockNameSuffix;
    BasicBlock *infiniteLoop = BasicBlock::Create(ctx, blockName, fcn);
    BranchInst::Create(infiniteLoop, infiniteLoop);
    vmap[ header ] = infiniteLoop;
  }

  // Loop exits:
  IntegerType *u32 = api.getU32();
  const Preprocess &preprocessor = getAnalysis< Preprocess >();
  const RecoveryFunction &recovery = preprocessor.getRecoveryFunction(loop);
  for(RecoveryFunction::CtrlEdgeNumbers::const_iterator i=recovery.exitNumbers.begin(), e=recovery.exitNumbers.end(); i!=e; ++i)
  {
    // Check if clone already exist for the exit block. If so, no need to create a new exit block

    BasicBlock           *clone = 0;
    const TerminatorInst *term = i->first.first;

    if ( vmap.find( term->getSuccessor(i->first.second) ) != vmap.end() )
      clone = dyn_cast<BasicBlock>( vmap[ term->getSuccessor(i->first.second) ] );

    if (clone)
    {
      // assert that clone has a misspec call

      bool has_misspec_call = false;
      for(BasicBlock::iterator ii=clone->begin() ; ii!=clone->end() ; ++ii)
      {
        CallInst* ci = dyn_cast<CallInst>(&*ii);
        if ( ci && ci->getCalledFunction()==api.getMisspeculate() )
          has_misspec_call = true;
      }

      assert( has_misspec_call && "==== Clone of loop exit should have a misspec call within it" );

      // Replace terminator to a UnreachableInst

      clone->getTerminator()->eraseFromParent();
      new UnreachableInst(ctx, clone);

      continue;
    }

    // Call _worker_finishes() to end this worker and announce which
    // loop exit was taken.
    unsigned exitNumber = i->second;
    Twine exitName = Twine("exit.") + Twine(exitNumber) + blockNameSuffix;
    BasicBlock *exitBB = BasicBlock::Create(ctx, exitName, fcn);

    // Clear all queues from which I might consume.
    for(unsigned prior=0; prior<stageno; ++prior)
    {
      Value *q_from_prior_stage = stage2queue[ prior ];
      if( ! q_from_prior_stage )
        continue;

      CallInst::Create(
          api.getClearQueue(),
          q_from_prior_stage,
          "",
          exitBB);
    }

    // Flush all queues to which I might produce.
    for(unsigned later=stageno+1, N=stage2queue.size(); later<N; ++later)
    {
      Value *q_to_later_stage = stage2queue[ later ];
      if( ! q_to_later_stage )
        continue;

      CallInst::Create(
        api.getFlushQueue(),
        q_to_later_stage,
        "",
        exitBB);
    }

    CallInst::Create(
      api.getWorkerFinishes(),
      ConstantInt::get(u32,exitNumber),
      "",
      exitBB);
    new UnreachableInst(ctx, exitBB);

    vmap[ term->getSuccessor(i->first.second) ] = exitBB;
  }

  // Loop entry
  for(Value::user_iterator i=header->user_begin(), e=header->user_end(); i!=e; ++i)
  {
    if( TerminatorInst *term = dyn_cast< TerminatorInst >( &**i ) )
      if( ! vmap.count( term->getParent() ) )
      {
        // sot: prevent issues due to broken critical edges.
        // If there is an loop entry from a backedge that goes through a dummy
        // basic block (introduced by break crit edges), then this dummy block
        // should not be mapped with the preheader but with the actual header

        // check if term->getParent() is a *_crit_edge basic block that leads to
        // a backedge

        // To have a backege one of the predecessors of term->getParent() need
        // to be the header
        bool isHeaderPred = false;
        for (BasicBlock *PredBB : predecessors(term->getParent())) {
          if (PredBB == header)
            isHeaderPred = true;
        }

        // dummy basic blocks added by break-crit-edges (LLVM pass) have
        // crit_edge as postfix
        std::string crit_edge_str = "crit_edge";
        std::string bbName = term->getParent()->getName().str();
        if (isHeaderPred && term->getParent()->getSingleSuccessor() &&
            (bbName.length() >= crit_edge_str.length() &&
             bbName.compare(bbName.length() - crit_edge_str.length(),
                            crit_edge_str.length(), crit_edge_str) == 0)) {
          vmap[term->getParent()] = vmap[header];
          //DEBUG(errs() << "header bb use: " << *(term->getParent())
          //             << " mapped with " << *vmap[header] << '\n');
        } else {
          vmap[term->getParent()] = preheader;
          //DEBUG(errs() << "header bb use: " << *(term->getParent())
          //             << " mapped with " << *preheader << '\n');
        }
      }
  }

  // Fix value references in this function.
  for(BBVec::iterator i=newBlocks.begin(), e=newBlocks.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;

    for(BasicBlock::iterator j=bb->begin(), jj=bb->end(); j!=jj; ++j)
    {
      Instruction *inst = &*j;

      // Make temporary copy since we're going to change the collection
      VSet operands;
      for(User::op_iterator k=inst->op_begin(), kk=inst->op_end(); k!=kk; ++k)
        operands.insert( &**k );
      for(VSet::iterator k=operands.begin(), kk=operands.end(); k!=kk; ++k)
      {
        Value *operand = &**k;

        if( vmap.count(operand) )
        {
          inst->replaceUsesOfWith(operand, vmap[operand]);
        }

        else if( BasicBlock *bb = dyn_cast< BasicBlock >(operand) )
        {
          ControlSpeculation::LoopBlock oldTarget = closestRelevantPostdom(bb,rel,pdt,cache_crpd);  // in 'origFcn'
          assert( oldTarget.isValid() && "closestRelevantPostdom invalid");

          BasicBlock *newTarget = 0;
          if( oldTarget.isLoopContinue() )
          {
            ControlSpeculation::LoopBlock iteration_begin = closestRelevantPostdom(header,rel,pdt,cache_crpd);
            assert( iteration_begin.isValid() && "closestRelevantPostdom invalid");

            if( iteration_begin.isAfterIteration() )
              iteration_begin = ControlSpeculation::LoopBlock( header );

            assert( iteration_begin.getBlock()
            && "iteration-begin block should have a non-null target block");

            newTarget = cast<BasicBlock>( vmap[ iteration_begin.getBlock() ] );
          }
          else
          {
            assert( oldTarget.getBlock()
            && "Non-loop-continue block should have a non-null target block");

            newTarget = cast<BasicBlock>( vmap[ oldTarget.getBlock() ] ); // same BB in 'fcn'
          }

          inst->replaceUsesOfWith(operand, newTarget);
        }
      }

      if( PHINode *phi = dyn_cast< PHINode >(inst) )
      {
        // PHI nodes do not 'use' their incoming basic blocks, so we must
        // perform the replaceUsesOfWith() manually.
        for(unsigned k=0, K=phi->getNumIncomingValues(); k<K; ++k)
        {
          BasicBlock *pred = phi->getIncomingBlock(k);
          ControlSpeculation::LoopBlock crd;
          if( pred == loop_preheader )
            crd = ControlSpeculation::LoopBlock( pred );
          else
            crd = closestRelevantDom(pred, rel, dt, cache_crd);

          assert( crd.isValid() && "closestRelevantDom invalid");

          if( !crd.isBeforeIteration() )
            if( vmap.count( crd.getBlock() ) )
              phi->setIncomingBlock(k, cast<BasicBlock>( vmap[ crd.getBlock() ] ) );
        }
      }
    }
  }

  // Initial values of phi nodes.
  Function::arg_iterator ai = fcn->arg_begin();
  for(unsigned i=0, N=liveIns.size(); i<N; ++i)
    ++ai;
  for(BasicBlock::iterator i=header->begin(), e=header->end(); i!=e; ++i, ++ai)
  {
    // PHI node in the original loop
    PHINode *phi = dyn_cast< PHINode >(&*i);
    if( !phi )
      break;

    VMap::iterator iter = vmap.find(phi);
    if( iter == vmap.end() )
      continue;

    // Clone of that PHI node in the thread
    PHINode *my_phi = dyn_cast< PHINode >( iter->second );
    if( !my_phi )
      continue; // (skip a consume of the phi node)

    const int idx = my_phi->getBasicBlockIndex(preheader);
    if( idx == -1 )
      continue;

    my_phi->setIncomingValue(idx, &*ai);
  }

  // How does this enter an iteration?
  //  If the loop header is relevant to that stage, we enter there; otherwise its CRPD.
  //  If this stage is empty, then we'll visit the infinite loop we created above.
  ControlSpeculation::LoopBlock iteration_begin = closestRelevantPostdom(header,rel,pdt,cache_crpd);
  assert( iteration_begin.isValid() && "closestRelevantPostdom invalid");

  if( iteration_begin.isAfterIteration() )
    iteration_begin = ControlSpeculation::LoopBlock( header );
  BranchInst::Create( cast<BasicBlock>( vmap[ iteration_begin.getBlock() ] ), preheader);

  // Final clean-up of PHI nodes.
  // Foreach PHI node in the newly generated code:
  for(BBVec::iterator i=newBlocks.begin(), e=newBlocks.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), jj=bb->end(); j!=jj; ++j)
    {
      Instruction *inst = &*j;
      PHINode *phi = dyn_cast< PHINode >(inst);
      if( !phi )
        break;

      // Because of control speculation, it may be that
      // one or more of PHI's incoming blocks cannot branch to bb.

      // Determine if PHI in 'bb' lists an incoming block which
      // cannot branch directly to 'bb.
      BBSet cutPreds;
      for(unsigned k=0, kk=phi->getNumIncomingValues(); k!=kk; ++k)
      {
        BasicBlock *pred = phi->getIncomingBlock(k);

        // Sanity test
        assert( pred->getParent() == bb->getParent()
        && "PHI has incoming block from a different function");

        bool predMayReachBB = false;
        TerminatorInst *term = pred->getTerminator();
        for(unsigned sn=0, SN=term->getNumSuccessors(); sn<SN; ++sn)
          if( term->getSuccessor(sn) == bb )
          {
            predMayReachBB = true;
            break;
          }

        if( !predMayReachBB )
          cutPreds.insert(pred);
      }
      // Cut those edges.
      for(BBSet::const_iterator k=cutPreds.begin(),kk=cutPreds.end(); k!=kk; ++k)
        phi->removeIncomingValue( *k, false );
    }
  }

  return preheader;
}

void MTCG::insertProduce(Api &api, BasicBlock *atEnd, Value *q, Value *v, MDNode *tag, bool toReplicatedStage)
{
  IntegerType *u32 = api.getU32(), *u64 = api.getU64();
  if( v->getType()->isPointerTy() )
    v = new PtrToIntInst(v,u64,"",atEnd);
  if( v->getType()->isFloatTy() )
    v = new BitCastInst(v,u32,"",atEnd);
  if( v->getType()->isDoubleTy() )
    v = new BitCastInst(v,u64,"",atEnd);
  if( IntegerType *intty = dyn_cast<IntegerType>(v->getType()) )
    if( intty->getBitWidth() < u64->getBitWidth() )
      v = new ZExtInst(v,u64,"",atEnd);
  assert( v->getType() == u64 && "Can't produce values of this type");

  Constant *prod = 0;
  if( toReplicatedStage )
  {
    prod = api.getProduceToReplicated();
    ++numProduceReplicated;
  }
  else
  {
    prod = api.getProduce();
    ++numProduce;
  }

  SmallVector<Value*,2> args(2);
  args[0] = q;
  args[1] = v;
  Instruction *call = CallInst::Create( prod, ArrayRef<Value*>(args),"",atEnd);

  if( tag )
    call->setMetadata("mtcg.channel", tag);
}

Value *MTCG::insertConsume(Api &api, BasicBlock *atEnd, Value *q, Type *ty, MDNode *tag, bool toReplicatedStage)
{
  Constant *cons = 0;
  if( toReplicatedStage )
  {
    cons = api.getConsumeInReplicated();
    ++numConsumeReplicated;
  }
  else
  {
    cons = api.getConsume();
    ++numConsume;
  }

  IntegerType *u32 = api.getU32();
  SmallVector<Value*,1> args(1);
  args[0] = q;
  Instruction *call = CallInst::Create(cons, ArrayRef<Value*>(args), "", atEnd);
  if( tag )
    call->setMetadata("mtcg.channel", tag);

  Value *v = call;
  if( IntegerType *intty = dyn_cast<IntegerType>(ty) )
    if( intty->getBitWidth() < cast<IntegerType>(v->getType())->getBitWidth() )
      v = new TruncInst(v,ty,"",atEnd);
  if( ty->isDoubleTy() )
    v = new BitCastInst(v,ty,"",atEnd);
  if( ty->isFloatTy() )
  {
    v = new TruncInst(v,u32,"",atEnd);
    v = new BitCastInst(v,ty,"",atEnd);
  }
  if( ty->isPointerTy() )
    v = new IntToPtrInst(v,ty,"",atEnd);
  assert( v->getType() == ty && "Can't consume values of this type");
  return v;
}

ControlSpeculation::LoopBlock MTCG::closestRelevantPostdom(BasicBlock *bb, const BBSet &rel, const LoopPostDom &pdt, MTCG::BB2LB &cache) const
{
  BB2LB::iterator i = cache.find(bb);
  if( i != cache.end() )
    return i->second;

  ControlSpeculation::LoopBlock iter = ControlSpeculation::LoopBlock( bb );
  while( !iter.isAfterIteration() && ! rel.count( iter.getBlock() ) )
  {
    ControlSpeculation::LoopBlock next = pdt.ipdom(iter);
    if( next == iter )
    {
      const Loop *loop = pdt.getLoop();
      const BasicBlock *header = loop->getHeader();
      const Function *fcn = header->getParent();

      errs() << "Discovered self-loop in post-dom tree...\n"
             << "       in loop: " << fcn->getName() << " :: " << header->getName() << '\n'
             << "  source block: " << bb->getName() << '\n'
             << " loop on block: " << iter << '\n';
      const ControlSpeculation::LoopBlock invalid = ControlSpeculation::LoopBlock();
      return cache[bb] = invalid;
    }
    iter = next;
  }

  return cache[bb] = iter;
}

ControlSpeculation::LoopBlock MTCG::closestRelevantDom(BasicBlock *bb, const BBSet &rel, const LoopDom &dt, MTCG::BB2LB &cache) const
{
  BB2LB::iterator i = cache.find(bb);
  if( i != cache.end() )
    return i->second;

  ControlSpeculation::LoopBlock iter = ControlSpeculation::LoopBlock( bb );
  while( !iter.isBeforeIteration() && ! rel.count( iter.getBlock() ) )
  {
    ControlSpeculation::LoopBlock next = dt.idom(iter);
    if( next == iter )
    {
      const Loop *loop = dt.getLoop();
      const BasicBlock *header = loop->getHeader();
      const Function *fcn = header->getParent();

      errs() << "Discovered self-loop in dom tree...\n"
             << "       in loop: " << fcn->getName() << " :: " << header->getName() << '\n'
             << "  source block: " << bb->getName() << '\n'
             << " loop on block: " << iter << '\n';
      const ControlSpeculation::LoopBlock invalid = ControlSpeculation::LoopBlock();
      return cache[bb] = invalid;
    }

    iter = next;
  }

  return cache[bb] = iter;
}

void MTCG::markIterationBoundaries(BasicBlock *preheader)
{
  Function *fcn = preheader->getParent();
  Module *mod = fcn->getParent();
  Api api(mod);
  LLVMContext &ctx = mod->getContext();
  BasicBlock *header = preheader->getTerminator()->getSuccessor(0);
  Constant *enditer = api.getEndIter();

  // Call begin iter at top of loop
  CallInst::Create( api.getBeginIter(), "", &*( header->getFirstInsertionPt() ) );

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  LoopInfo &li = mloops.getAnalysis_LoopInfo( fcn );
  Loop *loop = li.getLoopFor(header);

  // Identify the edges at the end of an iteration
  // == loop backedges, loop exits.
  typedef std::vector< RecoveryFunction::CtrlEdge > CtrlEdges;
  CtrlEdges iterationBounds;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();
    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      BasicBlock *dest = term->getSuccessor(sn);

      // Loop back edge
      if( dest == header )
        iterationBounds.push_back( RecoveryFunction::CtrlEdge(term,sn) );

      // Loop exit
      else if( ! loop->contains(dest) )
        iterationBounds.push_back( RecoveryFunction::CtrlEdge(term,sn) );
    }
  }

  for(unsigned i=0, N=iterationBounds.size(); i<N; ++i)
  {
    TerminatorInst *term = iterationBounds[i].first;
    BasicBlock *source = term->getParent();
    unsigned sn = iterationBounds[i].second;
    BasicBlock *dest = term->getSuccessor(sn);

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
      BranchInst::Create(dest,split);
    }
  }

  mloops.forget(fcn);
}

char MTCG::ID = 0;
static RegisterPass<MTCG> mtcg("specpriv-mtcg", "Multi-threaded code generation (MTCG), SpecPriv style");
}
}

