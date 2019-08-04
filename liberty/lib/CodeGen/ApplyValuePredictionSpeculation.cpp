#define DEBUG_TYPE "specpriv-transform"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Selector.h"
#include "liberty/Redux/Reduction.h"
#include "liberty/PointsToProfiler/Remat.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"
#include "liberty/Utilities/SplitEdge.h"
#include "liberty/Utilities/Timer.h"

#include "liberty/Speculation/Api.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Discriminator.h"
//#include "liberty/Speculation/PtrResidueManager.h"
#include "liberty/CodeGen/Preprocess.h"
#include "liberty/CodeGen/ApplyValuePredictionSpeculation.h"
#include "liberty/Speculation/Recovery.h"
#include "liberty/CodeGen/RoI.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numPrediction,  "Prediction values");

void ApplyValuePredSpec::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< ModuleLoops >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< ProfileGuidedPredictionSpeculator >();
  au.addRequired< Selector >();
  au.addRequired< Preprocess >();

  au.addPreserved< ModuleLoops >();
  au.addPreserved< ReadPass >();
  au.addPreserved< Selector >();
  au.addPreserved< Preprocess >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
  au.addPreserved< ProfileGuidedPredictionSpeculator >();
  //au.addPreserved< PtrResidueSpeculationManager >();
}


bool ApplyValuePredSpec::runOnModule(Module &module)
{
  DEBUG(errs() << "#################################################\n"
               << " ApplyValuePredSpec\n\n\n");
  mod = &module;
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Preprocess &preprocess = getAnalysis< Preprocess >();
  init(mloops);

  if( loops.empty() )
    return false;

  preprocess.assert_strategies_consistent_with_ir();
  bool modified = false;

  // Load-invariance speculation
  // - Value Speculation Checks

  // Perform per-loop preprocessing.

  for(unsigned i=0; i<loops.size(); ++i)
  {
    Loop *loop = loops[i];
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    DEBUG(errs() << "SpecPriv ApplyValuePredSpec: Processing loop "
      << fcn->getName() << " :: " << header->getName() << "\n");

    modified |= runOnLoop(loop);
  }

  if( modified )
  {
    // Invalidate LoopInfo-analysis, since we have changed
    // the code.
    const RoI &roi = preprocess.getRoI();
    for(RoI::FSet::iterator i=roi.fcns.begin(), e=roi.fcns.end(); i!=e; ++i)
      mloops.forget(*i);

    preprocess.assert_strategies_consistent_with_ir();
    DEBUG(errs() << "Successfully applied speculation to sequential IR\n");
  }

  return modified;
}

void ApplyValuePredSpec::init(ModuleLoops &mloops)
{
  LLVMContext &ctx = mod->getContext();

  voidty = Type::getVoidTy(ctx);
  u8 = Type::getInt8Ty(ctx);
  u16 = Type::getInt16Ty(ctx);
  u32 = Type::getInt32Ty(ctx);
  u64 = Type::getInt64Ty(ctx);
  voidptr = PointerType::getUnqual(u8);

  std::vector<Type *> formals;


  DEBUG(errs() << "SpecPriv ApplyValuePredSpec: Processing parallel region, consisting of:\n");

  // Identify loops we will parallelize
  const Selector &selector = getAnalysis< Selector >();
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    const BasicBlock *header = i->first;
    Function *fcn = const_cast< Function *>( header->getParent() );

    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);
    Loop *loop = li.getLoopFor(header);
    assert( loop->getHeader() == header );

    loops.push_back(loop);

    DEBUG(errs() << " - loop " << fcn->getName() << " :: " << header->getName() << "\n");
  }
}

bool ApplyValuePredSpec::runOnLoop(Loop *loop)
{
  BasicBlock *preheader = loop->getLoopPreheader();
  assert( preheader && "Run loop simplify first!");

  bool modified = false;

  modified |= manageMemOps(loop);

  return modified;
}

bool ApplyValuePredSpec::manageMemOps(Loop *loop)
{
  // Instrument the RoI:
  bool modified = false;

  // Insert value speculation checks
  modified |= addValueSpecChecks(loop);

  return modified;
}

bool ApplyValuePredSpec::deferIO()
{
  bool modified = false;
  Preprocess &preprocess = getAnalysis<Preprocess>();
  const RoI &roi = preprocess.getRoI();
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      CallSite cs = getCallSite(inst);
      if( !cs.getInstruction() )
        continue;

      Function *callee = cs.getCalledFunction();
      if( !callee )
        continue;

      const std::string &name = callee->getName();
      if( name != "printf"
      &&  name != "fprintf"
      &&  name != "fwrite"
      &&  name != "puts"
      &&  name != "putchar"
      &&  name != "fflush" )
        continue;

      Constant * f = Api(mod).getIO(callee);
      inst->replaceUsesOfWith(callee, f);
      ++numDeferredIO;
      modified = true;
    }
  }
  return modified;
}

bool ApplyValuePredSpec::addValueSpecChecks(Loop *loop)
{
  bool modified = false;

  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  Preprocess &preprocess = getAnalysis< Preprocess >();

  /* std::unordered_set<const Value *> *selectedLoadedValuePreds = */
  /*     preprocess.getSelectedLoadedValuePreds(header); */
  /* if (!selectedLoadedValuePreds) */
  /*   return modified; */

  /* auto selectedCtrlSpecDeps = preprocess.getSelectedCtrlSpecDeps(header); */

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  DominatorTree &dt = mloops.getAnalysis_DominatorTree( fcn );

  std::vector<BasicBlock*> entries;
  //std::vector<BasicBlock*> backedges;
  for(pred_iterator i=pred_begin(header), e=pred_end(header); i!=e; ++i)
    //if( loop->contains(*i) )
    //  backedges.push_back(*i);
    if (!loop->contains(*i))
      entries.push_back(*i);

  std::vector<BasicBlock *> endIter;
  for (Loop::block_iterator i = loop->block_begin(), e = loop->block_end();
       i != e; ++i) {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();
    for (unsigned sn = 0, N = term->getNumSuccessors(); sn < N; ++sn) {
      BasicBlock *dest = term->getSuccessor(sn);

      // Loop back edge
      if (dest == header)
        endIter.push_back(bb);

      // Loop exit
      else if (!loop->contains(dest))
        endIter.push_back(bb);
    }
  }

  const ProfileGuidedPredictionSpeculator &predspec = getAnalysis< ProfileGuidedPredictionSpeculator >();
  Remat remat;
  std::set<Value*> already;
  for(ProfileGuidedPredictionSpeculator::load_iterator i=predspec.loads_begin(loop), e=predspec.loads_end(loop); i!=e; ++i)
  {
    LoadInst *load = const_cast<LoadInst*>( i->second );

    Value *ptr = load->getPointerOperand();

    /* if (!selectedLoadedValuePreds->count(ptr)) */
    /*   continue; */

    if( already.count(ptr) )
      continue;
    already.insert(ptr);

    DEBUG(errs() << "Predicting that " << *load << " is loop invariant\n");

    // Outside of the parallel region.
    // Should rematerialize at loop pre-header.
    Value *new_ptr = 0;
    {
      Instruction *iptr = dyn_cast<Instruction>(ptr);
      if( iptr && iptr->getParent() == & fcn->getEntryBlock() )
          new_ptr = ptr;
      else
      {
        BasicBlock *preheader = loop->getLoopPreheader();
        assert(preheader && "Loop does not have a preheader");
        InstInsertPt where = InstInsertPt::End(preheader);
        new_ptr = remat.rematAtBlock(where, ptr, &dt);
      }
    }

    // Thas is undesirable.  I've commented it out, and instead we assert
    // that there must be a loop preheader.  Thus, we no longer need the PHI
    // since it can use the single name of the unique load of the predicted
    // value in the loop preheader.
    Value *new_prediction = 0;
    {
      BasicBlock *preheader = loop->getLoopPreheader();
      assert(preheader && "No loop preheader; you did not run loop simplify");

      // We assume there is a loop preheader
      LoadInst *prediction = new LoadInst(new_ptr, "predicted:" + load->getName() );
      InstInsertPt::End(preheader) << prediction;
      new_prediction = prediction;
    }


    // Part 1:
    InstInsertPt top = InstInsertPt::Before( header->getFirstNonPHI() );
    //  - in header, perform a store of the predicted value
    // this seems unnecessary
    Instruction *stpred = new StoreInst(new_prediction, new_ptr);
    top << stpred;
    preprocess.addToLPS(stpred, load);

    // Part 2:
    //  - at each backedge and exiting block,
    //    - load the pointer
    //    - compare to the predicted value.
    ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
    Constant *predict = Api(mod).getPredict();
    for(unsigned j=0; j<endIter.size(); ++j)
    {
      BasicBlock *pred = endIter[j];
      // do not add checks to header. header is guaranteed to have the correct
      // values if all other checks pass
      if (pred == header)
        continue;
      TerminatorInst *predT = dyn_cast<TerminatorInst>(pred->getTerminator());

      /* if (ctrlspec->isSpeculativelyDead(pred, header) && selectedCtrlSpecDeps && */
      /*     predT && selectedCtrlSpecDeps->count(predT)) */
      /*   continue; */
      if (ctrlspec->isSpeculativelyDead(pred, header) && predT )
        continue;

      // look if there a BB that stores redux and other loop-carried variables
      // (added by Preprocess)
      // if you find this kind of BB, insert value pred checks on predecessor
      // since this BB will be modified by MTCG
      std::string save_redux_lc_name = "save.redux.lc";
      std::string predBBName = pred->getName().str();
      if (predBBName.find(save_redux_lc_name) != std::string::npos) {
        pred = pred->getUniquePredecessor();
        assert(pred && "save.redux.lc BB does not have a unique predecessor??");
      }

      InstInsertPt bottom = InstInsertPt::End(pred);
      LoadInst *ldpred = new LoadInst(new_ptr);
      bottom << ldpred;
      preprocess.addToLPS(ldpred, load);
      Value *observed = ldpred;

      DEBUG(errs() << "Verifying at end of iteration that observed value "
        << *observed << " is invariant\n");

      Type *oty = observed->getType();
      if( oty->isIntegerTy() && ! oty->isIntegerTy(64) )
      {
        Instruction *cast = new ZExtInst(observed,u64);
        bottom << cast;
        preprocess.addToLPS(cast, load);
        observed = cast;
      }
      else if( oty->isPointerTy() )
      {
        Instruction *cast = new PtrToIntInst(observed,u64);
        bottom << cast;
        preprocess.addToLPS(cast, load);
        observed = cast;
      }

      Value *expected = new_prediction;
      Type *vty = expected->getType();
      if( vty->isIntegerTy() && ! vty->isIntegerTy(64) )
      {
        Instruction *cast = new ZExtInst(expected,u64);
        bottom << cast;
        preprocess.addToLPS(cast, load);
        expected = cast;
      }
      else if( vty->isPointerTy() )
      {
        Instruction *cast = new PtrToIntInst(expected,u64);
        bottom << cast;
        preprocess.addToLPS(cast, load);
        expected = cast;
      }

      Value *actuals[] = { observed, expected };
      Instruction *assert = CallInst::Create(predict, ArrayRef<Value*>(&actuals[0], &actuals[2]) );
      bottom << assert;
      preprocess.addToLPS(assert, load);
    }

    ++numPrediction;
    modified = true;
  }

  return modified;
}




char ApplyValuePredSpec::ID = 0;
static RegisterPass<ApplyValuePredSpec> x("spec-priv-apply-value-pred-spec",
  "Apply value-prediction speculation to RoI");
}
}

