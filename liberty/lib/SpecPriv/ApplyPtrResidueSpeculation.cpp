
#define DEBUG_TYPE "specpriv-transform"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/Read.h"
#include "liberty/SpecPriv/Reduction.h"
#include "liberty/SpecPriv/Selector.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"
#include "liberty/Utilities/SplitEdge.h"
#include "liberty/Utilities/Timer.h"

#include "Api.h"
#include "Classify.h"
#include "Discriminator.h"
#include "PtrResidueManager.h"
#include "Preprocess.h"
#include "ApplyPtrResidueSpeculation.h"
#include "Recovery.h"
#include "RoI.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numPtrResidue,  "Ptr-residue tests inserted");

void ApplyPtrResidueSpec::getAnalysisUsage(AnalysisUsage &au) const
{
  //au.addRequired< DataLayout >();
  au.addRequired< ModuleLoops >();
  au.addRequired< PtrResidueSpeculationManager >();
  au.addRequired< Selector >();
  au.addRequired< Preprocess >();

  au.addPreserved< ModuleLoops >();
  au.addPreserved< ReadPass >();
  au.addPreserved< PtrResidueSpeculationManager >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
  au.addPreserved< ProfileGuidedPredictionSpeculator >();
  au.addPreserved< Selector >();
  au.addPreserved< Preprocess >();
}


bool ApplyPtrResidueSpec::runOnModule(Module &module)
{
  DEBUG(errs() << "#################################################\n"
               << " ApplyPtrResidueSpec\n\n\n");
  mod = &module;
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Preprocess &preprocess = getAnalysis< Preprocess >();
  init(mloops);

  if( loops.empty() )
    return false;

  preprocess.assert_strategies_consistent_with_ir();
  bool modified = false;

  // Pointer residue speculation:
  // - Ptr Residue Checks

  // Perform per-loop preprocessing.

  for(unsigned i=0; i<loops.size(); ++i)
  {
    Loop *loop = loops[i];
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    DEBUG(errs() << "SpecPriv ApplyPtrResidueSpec: Processing loop "
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

void ApplyPtrResidueSpec::init(ModuleLoops &mloops)
{
  LLVMContext &ctx = mod->getContext();

  voidty = Type::getVoidTy(ctx);
  IntegerType *u8 = Type::getInt8Ty(ctx);
  u16 = Type::getInt16Ty(ctx);
  voidptr = PointerType::getUnqual(u8);

  std::vector<Type *> formals;


  DEBUG(errs() << "SpecPriv ApplyPtrResidueSpec: Processing parallel region, consisting of:\n");

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

bool ApplyPtrResidueSpec::runOnLoop(Loop *loop)
{
  BasicBlock *preheader = loop->getLoopPreheader();
  assert( preheader && "Run loop simplify first!");

  bool modified = false;

  modified |= manageMemOps(loop);

  return modified;
}

bool ApplyPtrResidueSpec::manageMemOps(Loop *loop)
{
  // Instrument the RoI:
  bool modified = false;


  // Insert pointer-residue checks
  modified |= addPtrResidueChecks(loop);

  return modified;
}

bool ApplyPtrResidueSpec::addPtrResidueChecks(Loop *loop)
{
  bool modified = false;

  Preprocess &preprocess = getAnalysis< Preprocess >();
  RoI &roi = preprocess.getRoI();
  PtrResidueSpeculationManager &manager = getAnalysis< PtrResidueSpeculationManager >();
  const Read &spresults = manager.getSpecPrivResult();
  const Ctx *cc = spresults.getCtx(loop);

  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();
  Module *mod = fcn->getParent();
  Api api(mod);
  Constant *ptrresidue = api.getPtrResidue();

  for(PtrResidueSpeculationManager::iterator i=manager.begin(), e=manager.end(); i!=e; ++i)
  {
    const Ctx *ctx = i->second;
//    errs() << "Ptr residue check in ctx " << *ctx << '\n'
//           << " versus ctx " << *cc << '\n';
    if( !ctx->matches(cc) )
      continue;

    Value *ptr = const_cast<Value*>( i->first );
    const uint16_t residue = spresults.getPointerResiduals(ptr,cc);

    Instruction *def = dyn_cast< Instruction >(ptr);
    if( !def )
      continue;

    if( !roi.bbs.count( def->getParent() ) )
      continue;

    InstInsertPt where = InstInsertPt::After(def);
    if( ptr->getType() != voidptr )
    {
      Instruction *cast = new BitCastInst(ptr,voidptr);
      where << cast;
      preprocess.addToLPS(cast, def);
      ptr = cast;
    }

    std::string str;
    raw_string_ostream sout(str);
    sout << "Pointer-residue violation on pointer ";
    if( ptr->hasName() )
      sout << ptr->getName();
    else
      sout << *ptr;

    sout << " in " << where.getFunction()->getName()
         << " :: " << where.getBlock()->getName()
         << "; should be " << residue;
    Constant *message = getStringLiteralExpression(*mod, sout.str());

    Value *actuals[3];
    actuals[0] = ptr;
    actuals[1] = ConstantInt::get(u16,residue);
    actuals[2] = message;

    Instruction *call = CallInst::Create(ptrresidue, ArrayRef<Value*>(&actuals[0], &actuals[3]));
    where << call;
    preprocess.addToLPS(call, def);

    modified = true;
    ++numPtrResidue;
  }

  return modified;
}


char ApplyPtrResidueSpec::ID = 0;
static RegisterPass<ApplyPtrResidueSpec> x("spec-priv-apply-ptr-residue-spec",
  "Apply pointer-residue speculation to RoI");
}
}

