#define DEBUG_TYPE "header-phi-remed"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/HeaderPhiPredRemed.h"

#define DEFAULT_HEADER_PHI_PRED_REMED_COST 40

namespace liberty {
using namespace llvm;

STATISTIC(numNoLCRegDep, "Number of lc reg deps removed by header-phi-remed");

void HeaderPhiPredRemedy::apply(Task *task) {
  // TODO: transfer the code for application of header-phi-remed here.
}

bool HeaderPhiPredRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<HeaderPhiPredRemedy> hphiPredRhs =
      std::static_pointer_cast<HeaderPhiPredRemedy>(rhs);
  return this->predPHI < hphiPredRhs->predPHI;
}

Remediator::RemedResp HeaderPhiPredRemediator::regdep(const Instruction *A,
                                                      const Instruction *B,
                                                      bool loopCarried,
                                                      const Loop *L) {
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  std::shared_ptr<HeaderPhiPredRemedy> remedy =
      std::shared_ptr<HeaderPhiPredRemedy>(new HeaderPhiPredRemedy());
  remedy->cost = DEFAULT_HEADER_PHI_PRED_REMED_COST;

  // check if the instruction that sinks the register dependence has a
  // predictable value.
  // header phi remediator only examines predictability of header phi nodes
  // values.
  if (loopCarried && predspec->isPredictable(B, L)) {
    ++numNoLCRegDep;
    remedy->predPHI = dyn_cast<PHINode>(B);
    assert(remedy->predPHI &&
           "HeaderPhiPredRemediator predicts only values of phi nodes");
    remedResp.depRes = DepResult::NoDep;

    DEBUG(errs() << "HeaderPhiPredRemed removed reg dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
