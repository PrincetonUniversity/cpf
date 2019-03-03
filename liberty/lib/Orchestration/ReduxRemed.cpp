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
STATISTIC(numMemDepsRemovedRedux,             "Num mem deps removed");

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

void ReduxRemediator::findMemReductions(Loop *l) {

  std::set<Value *> visitedAccums;
  const std::vector<Loop *> subloops = l->getSubLoops();

  for (Loop::block_iterator bbi = l->block_begin(), bbe = l->block_end();
       bbi != bbe; ++bbi) {
    BasicBlock *bb = *bbi;
    // Exclude instructions in one of subloops
    bool withinSubloop = false;
    for (auto &sl : subloops) {
      if (sl->contains(bb)) {
        withinSubloop = true;
        break;
      }
    }
    if (withinSubloop)
      continue;

    for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
      LoadInst *load = dyn_cast<LoadInst>(i);
      if (!load)
        continue;

      Value *accum = load->getPointerOperand();
      if (visitedAccums.count(accum))
        continue;
      visitedAccums.insert(accum);

      // Pointer to accumulator must be loop invariant
      if (!l->isLoopInvariant(accum))
        continue;

      const BinaryOperator *add = nullptr;
      const CmpInst *cmp = nullptr;
      const BranchInst *br = nullptr;
      const StoreInst *store = nullptr;
      const Reduction::Type type =
          Reduction::isReductionLoad(load, &add, &cmp, &br, &store);
      if (!type)
        continue;

      // Looks like a reduction.
      // Next, we will use static analysis to ensure that
      //  for every other memory operation in this loop, either:
      //   a. the operation is a reduction operation of the same type, or
      //   b. the operation does not access this accumulator.
      if (!Reduction::allOtherAccessesAreReduction(l, type, accum, loopAA))
        continue;

      // Now this is a reduction
      // This should be either add reduction or min/max reduction
      memReductions.insert(store);
    }
  }
}

bool ReduxRemediator::isMemReduction(const Instruction *I) {
  const StoreInst *sI = dyn_cast<StoreInst>(I);
  if (!sI)
    return false;
  if (memReductions.count(sI))
    return true;

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

  /*
  Function *F = ncA->getParent()->getParent();
  if (F->getFnAttribute("no-nans-fp-math").getValueAsString() == "false") {
    errs() << "THe no-nans-fp-math flag was not set!\n";
    F->addFnAttr("no-nans-fp-math", "true");
  }
  if (F->getFnAttribute("unsafe-fp-math").getValueAsString() == "false") {
    errs() << "THe unsafe-fp-math flag was not set!\n";
    F->addFnAttr("unsafe-fp-math", "true");
  }
  */

  //errs() << "  Redux remed examining edge(s) from " << *A << " to " << *B
  //       << '\n';

  auto aSCC = loopDepInfo->loopSCCDAG->sccOfValue(ncA);
  auto bSCC = loopDepInfo->loopSCCDAG->sccOfValue(ncB);
  if (aSCC == bSCC && loopDepInfo->sccdagAttrs.canExecuteReducibly(aSCC)) {
    ++numRegDepsRemovedNoelleRedux;
    DEBUG(errs() << "Resolved by noelle Redux\n");
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
    DEBUG(errs() << "Resolved by liberty sumRedux\n");
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
    DEBUG(errs() << "Resolved by liberty MinMaxRedux\n");
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
    DEBUG(errs() << "Resolved by liberty (specpriv but hopefully conservative) redux detection (intra-iteration)\n");
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
    DEBUG(errs() << "Resolved by liberty (specpriv but hopefully conservative) redux detection (loop-carried)\n");
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
        DEBUG(errs() << "Resolved by llvm redux detection (intra-iteration)\n");
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
        DEBUG(errs() << "Resolved by llvm redux detection (loop-carried)\n");
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

Remediator::RemedResp ReduxRemediator::memdep(const Instruction *A,
                                              const Instruction *B,
                                              bool LoopCarried, bool RAW,
                                              const Loop *L) {

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  auto remedy = make_shared<ReduxRemedy>();
  remedy->cost = DEFAULT_REDUX_REMED_COST;

  if (isMemReduction(A)) {
    ++numMemDepsRemovedRedux;
    DEBUG(errs() << "Removed mem dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->reduxI = A;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  if (isMemReduction(B)) {
    ++numMemDepsRemovedRedux;
    DEBUG(errs() << "Removed mem dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->reduxI = B;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
