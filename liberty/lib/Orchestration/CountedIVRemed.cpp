#define DEBUG_TYPE "counted-iv-remed"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/CountedIVRemed.h"

#define DEFAULT_COUNTED_IV_REMED_COST 0

namespace liberty {
using namespace llvm;

STATISTIC(numNoRegDep, "Number of reg deps removed by counted IV remed");
STATISTIC(numNoCtrlDep, "Number of ctrl deps removed by counted IV remed");

void CountedIVRemedy::apply(Task *task) {
}

bool CountedIVRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<CountedIVRemedy> countedIVRhs =
      std::static_pointer_cast<CountedIVRemedy>(rhs);
  return this->ivSCC < countedIVRhs->ivSCC;
}

Remediator::RemedResp CountedIVRemediator::regdep(const Instruction *A,
                                                  const Instruction *B,
                                                  bool loopCarried,
                                                  const Loop *L) {
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  if (!loopCarried)
    return remedResp;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = DEFAULT_COUNTED_IV_REMED_COST;

  auto aSCC = loopDepInfo->loopSCCDAG->sccOfValue(const_cast<Instruction *>(A));
  auto bSCC = loopDepInfo->loopSCCDAG->sccOfValue(const_cast<Instruction *>(B));

  if (aSCC == bSCC && loopDepInfo->sccdagAttrs.isInductionVariableSCC(aSCC) &&
      loopDepInfo->sccdagAttrs.isLoopGovernedByIV() &&
      loopDepInfo->sccdagAttrs.sccIVBounds.find(aSCC) !=
          loopDepInfo->sccdagAttrs.sccIVBounds.end()) {
    ++numNoRegDep;
    remedy->ivSCC = aSCC;
    const PHINode *phiB = dyn_cast<PHINode>(B);
    assert(phiB && "Dest inst in CountedIVRemediator::regdep not a phiNode??");
    remedy->ivPHI = phiB;
    remedResp.depRes = DepResult::NoDep;
    DEBUG(errs() << "CountedIVRemed removed reg dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedResp CountedIVRemediator::ctrldep(const Instruction *A,
                                                   const Instruction *B,
                                                   const Loop *L) {

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = DEFAULT_COUNTED_IV_REMED_COST;

  auto aSCC = loopDepInfo->loopSCCDAG->sccOfValue(const_cast<Instruction *>(A));

  // remove all ctrl edges originating from branch controlled by a bounded IV
  if (loopDepInfo->sccdagAttrs.isInductionVariableSCC(aSCC) &&
      loopDepInfo->sccdagAttrs.isLoopGovernedByIV() &&
      loopDepInfo->sccdagAttrs.sccIVBounds.find(aSCC) !=
          loopDepInfo->sccdagAttrs.sccIVBounds.end()) {
    ++numNoCtrlDep;
    remedy->ivSCC = aSCC;
    remedResp.depRes = DepResult::NoDep;
    DEBUG(errs() << "CountedIVRemed removed ctrl dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
