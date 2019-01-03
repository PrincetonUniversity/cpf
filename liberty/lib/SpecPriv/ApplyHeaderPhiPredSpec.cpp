#define DEBUG_TYPE "specpriv-transform"

#include "ApplyHeaderPhiPredSpec.h"

#include "liberty/SpecPriv/Selector.h"
#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/Remat.h"
#include "liberty/Utilities/CastUtil.h"
#include "liberty/SpecPriv/SmtxSlampManager.h"

#include "Api.h"
#include "HeaderPhiPredictionSpeculation.h"
#include "Preprocess.h"
#include "PtrResidueManager.h"
#include "PrivateerSelector.h"
#include "NoSpecSelector.h"
#include "SmtxManager.h"
#include "RemedSelector.h"
#include "SmtxSelector.h"
#include "Smtx2Selector.h"
#include "RoI.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

void ApplyHeaderPhiPredSpec::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< TargetLibraryInfoWrapperPass >();
  au.addRequired< ModuleLoops >();
  au.addRequired< HeaderPhiPredictionSpeculation >();
  au.addRequired< Selector >();
  au.addRequired< Preprocess >();

  //au.addPreserved< ReadPass >();
  au.addPreserved< ModuleLoops >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
  //au.addPreserved< ProfileGuidedPredictionSpeculator >();
  au.addPreserved< HeaderPhiPredictionSpeculation >();
  au.addPreserved< Preprocess >();
  au.addPreserved< Selector >();
 // au.addPreserved< SmtxSpeculationManager >();
  au.addPreserved< SmtxSlampSpeculationManager >();
  //au.addPreserved< PtrResidueSpeculationManager >();
  au.addPreserved< NoSpecSelector >();
  au.addPreserved< RemedSelector >();
  //au.addPreserved< SpecPrivSelector >();
  //au.addPreserved< SmtxSelector >();
}

bool ApplyHeaderPhiPredSpec::runOnModule(Module& m)
{
  DEBUG(errs() << "#################################################\n"
               << " ApplyHeaderPhiPredSpec\n\n\n");

  this->m = &m;

  ModuleLoops& mloops = getAnalysis< ModuleLoops >();
  Preprocess&  preprocess = getAnalysis< Preprocess >();

  init(mloops);

  if( this->loops.empty() )
    return false;

  preprocess.assert_strategies_consistent_with_ir();
  bool modified = false;

  // Perform per-loop preprocessing.

  for(unsigned i=0; i<this->loops.size(); ++i)
  {
    Loop*       loop = this->loops[i];
    BasicBlock* header = loop->getHeader();
    Function*   fcn = header->getParent();

    DEBUG(errs() << "SpecPriv ApplyHeaderPhiPredSpec: Processing loop "
      << fcn->getName() << " :: " << header->getName() << "\n");

    modified |= runOnLoop(loop);
  }

  if( modified )
  {
    // Invalidate LoopInfo-analysis, since we have changed
    // the code.
    const RoI& roi = preprocess.getRoI();

    for(RoI::FSet::iterator i=roi.fcns.begin(), e=roi.fcns.end(); i!=e; ++i)
      mloops.forget(*i);

    preprocess.assert_strategies_consistent_with_ir();
    DEBUG(errs() << "Successfully applied speculation to sequential IR\n");
  }

  return modified;
}

void ApplyHeaderPhiPredSpec::init(ModuleLoops& mloops)
{
  LLVMContext& ctx = m->getContext();

  this->voidty = Type::getVoidTy(ctx);
  this->u8 = Type::getInt8Ty(ctx);
  this->u16 = Type::getInt16Ty(ctx);
  this->u32 = Type::getInt32Ty(ctx);
  this->u64 = Type::getInt64Ty(ctx);
  this->voidptr = PointerType::getUnqual(u8);

  DEBUG(errs() << "SpecPriv ApplyHeaderPhiPredSpec: Processing parallel region, consisting of:\n");

  // Identify loops we will parallelize
  const Selector& selector = getAnalysis< Selector >();
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    const BasicBlock* header = i->first;
    Function*         fcn = const_cast< Function *>( header->getParent() );

    LoopInfo& loopinfo = mloops.getAnalysis_LoopInfo(fcn);
    Loop*     loop = loopinfo.getLoopFor(header);
    assert( loop->getHeader() == header );

    this->loops.push_back(loop);

    DEBUG(errs() << " - loop " << fcn->getName() << " :: " << header->getName() << "\n");
  }
}

bool ApplyHeaderPhiPredSpec::runOnLoop(Loop* loop)
{
  BasicBlock* preheader = loop->getLoopPreheader();
  BasicBlock* latch = loop->getLoopLatch();

  assert( (preheader && latch) && "Run loop simplify first!");

  BasicBlock* header = loop->getHeader();
  Function*   fcn = header->getParent();

  bool modified = false;

  HeaderPhiPredictionSpeculation& predspec = getAnalysis< HeaderPhiPredictionSpeculation >();
  Preprocess&                     preprocess = getAnalysis< Preprocess >();
  ModuleLoops&                    mloops = getAnalysis< ModuleLoops >();
  DominatorTree&                  dt = mloops.getAnalysis_DominatorTree( fcn );

  Remat              remat;
  std::set<Value*>   already;
  std::set<PHINode*> instrumented;

  // apply speculation for phi where it supposed to have the same value for every iteration. one
  // good thing is that this is actually "not" a speculation: As far as all other speculative
  // assumptions that HeaderPhiPredictionSpeculation relies on hold, transformation below is
  // guaranteed to be valid.

  for(HeaderPhiPredictionSpeculation::phi_iterator i=predspec.phi_begin(loop), e=predspec.phi_end(loop); i!=e; ++i)
  {
    PHINode* phi = i->second;

    phi->setIncomingValue( phi->getBasicBlockIndex(latch), phi->getIncomingValueForBlock(preheader) );
    instrumented.insert( phi );
  }

  // apply speculation for phi nodes that profiled to be generate same value across iterations

  for(HeaderPhiPredictionSpeculation::phi_iterator i=predspec.spec_phi_begin(loop), e=predspec.spec_phi_end(loop); i!=e; ++i)
  {
    PHINode* phi = i->second;

    if ( instrumented.count( phi ) )
      continue;

    Value* frompreheader = phi->getIncomingValueForBlock( preheader );
    Value* fromlatch = phi->getIncomingValueForBlock( latch );

    // insert a checker at the end of loop latch

    Constant*    predict = Api(this->m).getPredict();
    InstInsertPt pt = InstInsertPt::Before( latch->getTerminator() );

    Instruction* castexpected = dyn_cast<Instruction>( castToInt64Ty(frompreheader, pt) );

    assert(castexpected);
    preprocess.addToLPS(castexpected, latch->getTerminator());

    Instruction* castobserved = dyn_cast<Instruction>( castToInt64Ty(fromlatch, pt) );

    assert(castobserved);
    preprocess.addToLPS(castobserved, latch->getTerminator());

    Value*       actuals[] = { castobserved, castexpected };
    Instruction* assert = CallInst::Create(predict, ArrayRef<Value*>(&actuals[0], &actuals[2]) );

    pt << assert;
    preprocess.addToLPS(assert, latch->getTerminator());

    modified = true;

    phi->setIncomingValue( phi->getBasicBlockIndex(latch), phi->getIncomingValueForBlock(preheader) );
    instrumented.insert( phi );
  }

  // apply speculation for phi-load pair where the load that eventually feeds the phi is supposed to
  // be a live-in

  for(HeaderPhiPredictionSpeculation::pair_iterator i=predspec.pair_begin(loop), e=predspec.pair_end(loop); i!=e; ++i)
  {
    HeaderPhiPredictionSpeculation::PhiLoadPair p = i->second;

    PHINode*        phi = p.first;
    LoadInst*       load = p.second;
    Value*          ptr = load->getPointerOperand();

    if ( instrumented.count( phi ) )
      continue;

    DEBUG(errs() << "Predicting that " << *load << " is loop invariant\n");

    // Outside of the parallel region.
    // Should rematerialize at loop pre-header.

    Value* new_ptr = 0;
    {
      const Instruction* iptr = dyn_cast<Instruction>(ptr);
      if( iptr && iptr->getParent() == & fcn->getEntryBlock() )
          new_ptr = ptr;
      else
      {
        InstInsertPt where = InstInsertPt::End(preheader);
        new_ptr = remat.rematAtBlock(where, ptr, &dt);
      }
    }

    Value* new_prediction = 0;
    {
      LoadInst* prediction = new LoadInst(new_ptr, "predicted:" + load->getName() );
      InstInsertPt::End(preheader) << prediction;
      new_prediction = prediction;
    }

    // replace phi incoming from latch to a predicted value

    phi->setIncomingValue( phi->getBasicBlockIndex(latch), new_prediction );

    // insert a checker after the load

    Constant*    predict = Api(this->m).getPredict();
    InstInsertPt pt = InstInsertPt::After(load);

    Instruction* castexpected = dyn_cast<Instruction>( castToInt64Ty(new_prediction, pt) );

    assert(castexpected);
    preprocess.addToLPS(castexpected, load);

    Instruction* castobserved = dyn_cast<Instruction>( castToInt64Ty(load, pt) );

    assert(castobserved);
    preprocess.addToLPS(castobserved, load);

    Value*       actuals[] = { castobserved, castexpected };
    Instruction* assert = CallInst::Create(predict, ArrayRef<Value*>(&actuals[0], &actuals[2]) );

    pt << assert;
    preprocess.addToLPS(assert, load);

    modified = true;
  }

  return modified;
}

char ApplyHeaderPhiPredSpec::ID = 0;
static RegisterPass<ApplyHeaderPhiPredSpec> x("spec-priv-apply-header-phi-pred-spec",
  "Apply header phi prediction speculation to RoI");
}

}
