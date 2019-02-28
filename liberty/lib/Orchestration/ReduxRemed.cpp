#define DEBUG_TYPE "redux-remed"

#include "liberty/Analysis/Introspection.h"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include "liberty/Orchestration/ReduxRemed.h"
#include "liberty/Analysis/ReductionDetection.h"

#define DEFAULT_REDUX_REMED_COST 2

namespace liberty
{
using namespace llvm;
using namespace SpecPriv;

STATISTIC(numRegQueries,                      "Num register deps queries");
STATISTIC(numRegDepsRemovedSumRedux,          "Num reg deps removed with sum reduction");
STATISTIC(numRegDepsRemovedMinMaxRedux,       "Num reg deps removed with min/max reduction");
STATISTIC(numRegDepsRemovedLLVMRedux,         "Num reg deps removed with llvm's reduction identification");
STATISTIC(numRegDepsRemovedNoelleRedux,       "Num reg deps removed with noelle redux");
STATISTIC(numRegDepsRemovedRedux,             "Num reg deps removed with liberty redux");

void ReduxRemedy::apply(llvm::PDG &pdg) {
  // TODO: transfer the code for application of redux here.
}

bool ReduxRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<ReduxRemedy> reduxRhs =
      std::static_pointer_cast<ReduxRemedy>(rhs);
  if (this->reduxI == nullptr && reduxRhs->reduxI == nullptr)
    return this->reduxSCC < reduxRhs->reduxSCC;
  else if (this->reduxI != nullptr && reduxRhs->reduxI != nullptr)
    return this->reduxI < reduxRhs->reduxI;
  else
    return (this->reduxI == nullptr);
}

bool ReduxRemediator::isRegReductionPHI(Instruction *I, Loop *l) {
  PHINode *phi = dyn_cast<PHINode>(I);
  if (phi == nullptr)
    return false;
  if (l->getHeader() != I->getParent())
    return false;
  // check if result is cached
  if (regReductions.count(I))
    return true;

  std::set<PHINode*> ignore;
  VSet phis, binops, cmps, brs, liveOuts;
  BinaryOperator::BinaryOps opcode;
  Reduction::Type type;
  Value *initVal = 0;
  if ( Reduction::isRegisterReduction(
      *se, l, phi, nullptr, ignore, type, opcode, phis, binops, cmps, brs,
      liveOuts, initVal) )
  {
    Instruction *reduxI = phi;
    regReductions.insert(reduxI);

    errs() << "Found a register reduction:\n"
           << "          PHI: " << *phi << '\n'
           << "      Initial: " << *initVal << '\n'
           << "    Internals:\n";
    for(VSet::iterator i=phis.begin(), e=phis.end(); i!=e;  ++i)
      errs() << "            o " << **i << '\n';
    errs() << "      Updates:\n";
    for(VSet::iterator i=binops.begin(), e=binops.end(); i!=e;  ++i)
      errs() << "            o " << **i << '\n';

    errs() << "    Live-outs:\n";
    for(VSet::iterator i=liveOuts.begin(), e=liveOuts.end(); i!=e;  ++i)
      errs() << "            o " << **i << '\n';

      return true;
  }
  return false;
}


// there can be RAW reg deps
Remediator::RemedResp ReduxRemediator::regdep(const Instruction *A,
                                              const Instruction *B,
                                              bool loopCarried, const Loop *L) {

  ++numRegQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  auto remedy = make_shared<ReduxRemedy>();
  remedy->cost = DEFAULT_REDUX_REMED_COST;

  //const bool loopCarried = (B->getParent() == L->getHeader() && isa<PHINode>(B));

  // if fast math for floating point is not allowed it enable it for current
  // function
  // TODO: eventually could actually ask the user, but that will lower the
  // preference for this pass. Alternatively, could also compile with -ffast-math
  // flag. Compiling with the flag will produce more optimal code overall and
  // should eventually be used.

  Instruction *ncA = const_cast<Instruction*>(A);
  Instruction *ncB = const_cast<Instruction*>(B);
  Loop *ncL = const_cast<Loop *>(L);

  Function *F = ncA->getParent()->getParent();
  if (F->getFnAttribute("no-nans-fp-math").getValueAsString() == "false") {
    errs() << "THe no-nans-fp-math flag was not set!\n";
    F->addFnAttr("no-nans-fp-math", "true");
  }
  if (F->getFnAttribute("unsafe-fp-math").getValueAsString() == "false") {
    errs() << "THe unsafe-fp-math flag was not set!\n";
    F->addFnAttr("unsafe-fp-math", "true");
  }

  //errs() << "  Redux remed examining edge(s) from " << *A << " to " << *B
  //       << '\n';

  auto aSCC = loopDepInfo->loopSCCDAG->sccOfValue(ncA);
  auto bSCC = loopDepInfo->loopSCCDAG->sccOfValue(ncB);
  if (aSCC == bSCC && loopDepInfo->sccdagAttrs.canExecuteReducibly(aSCC)) {
    ++numRegDepsRemovedNoelleRedux;
    errs() << "Resolved by noelle Redux\n";
    DEBUG(errs() << "Removed reg dep between inst " << *A
                 << "  and  " << *B << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->reduxSCC = aSCC;
    remedy->reduxI = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  ReductionDetection reduxdet;
  if (reduxdet.isSumReduction(L, A, B, loopCarried)) {
    ++numRegDepsRemovedSumRedux;
    errs() << "Resolved by liberty sumRedux\n";
    DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    if (loopCarried)
      remedy->reduxI = A;
    else
      remedy->reduxI = B;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }
  if (reduxdet.isMinMaxReduction(L, A, B, loopCarried)) {
    ++numRegDepsRemovedMinMaxRedux;
    errs() << "Resolved by liberty MinMaxRedux\n";
    DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    if (loopCarried)
      remedy->reduxI = A;
    else
      remedy->reduxI = B;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  // use Nick's Redux
  if (isRegReductionPHI(ncA, ncL)) {
    // A: x0 = phi(initial from outside loop, x1 from backedge)
    // B: x1 = x0 + ..
    // Intra iteration dep removed
    ++numRegDepsRemovedRedux;
    errs() << "Resolved by liberty (specpriv but hopefully conservative) redux detection (intra-iteration)\n";
    DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->reduxI = B;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }
  if (isRegReductionPHI(ncB, ncL)) {
    // B: x0 = phi(initial from outside loop, x1 from backedge)
    // A: x1 = x0 + ..
    // Loop-carried dep removed
    ++numRegDepsRemovedRedux;
    errs() << "Resolved by liberty (specpriv but hopefully conservative) redux detection (loop-carried)\n";
    DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->reduxI = A;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  // already know that instruction A is an operand of instruction B
  RecurrenceDescriptor recdes;

  // since we run loop-simplify
  // before applying the remediators,
  // loops should be in canonical
  // form and have preHeaders. In
  // some cases though, loopSimplify
  // is unable to canonicalize some
  // loops. Thus we need to check
  // first

  if (L->getLoopPreheader()) {
    if (PHINode *PhiA = dyn_cast<PHINode>(ncA)) {
      if (RecurrenceDescriptor::isReductionPHI(PhiA, ncL,
                                               recdes)) {
        // A: x0 = phi(initial from outside loop, x1 from backedge)
        // B: x1 = x0 + ..
        // Intra iteration dep removed
        ++numRegDepsRemovedLLVMRedux;
        errs() << "Resolved by llvm redux detection (intra-iteration)\n";
        DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                     << '\n');
        remedResp.depRes = DepResult::NoDep;
        remedy->reduxI = B;
        remedy->reduxSCC = nullptr;
        remedResp.remedy = remedy;
        return remedResp;
      }
    }
    if (PHINode *PhiB = dyn_cast<PHINode>(ncB)) {
      if (RecurrenceDescriptor::isReductionPHI(PhiB, ncL,
                                               recdes)) {
        // B: x0 = phi(initial from outside loop, x1 from backedge)
        // A: x1 = x0 + ..
        // Loop-carried dep removed
        ++numRegDepsRemovedLLVMRedux;
        errs() << "Resolved by llvm redux detection (loop-carried)\n";
        DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                     << '\n');
        remedResp.depRes = DepResult::NoDep;
        remedy->reduxI = A;
        remedy->reduxSCC = nullptr;
        remedResp.remedy = remedy;
        return remedResp;
      }
    }
  }
  //errs() << "Redux remed unable to resolve this dep\n";
  remedResp.remedy = remedy;
  return remedResp;
}
} // namespace liberty
