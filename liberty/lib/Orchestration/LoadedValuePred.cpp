#define DEBUG_TYPE "loaded-value-pred-remed"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/LoadedValuePredRemed.h"

#define DEFAULT_LOADED_VALUE_PRED_REMED_COST 40

namespace liberty {
using namespace llvm;

STATISTIC(numNoMemDep, "Number of mem deps removed by loaded-value-pred-remed");

void LoadedValuePredRemedy::apply(Task *task) {
  // TODO: code for application of loaded-value-pred-remed here.
}

bool LoadedValuePredRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<LoadedValuePredRemedy> valPredRhs =
      std::static_pointer_cast<LoadedValuePredRemedy>(rhs);
  return this->loadI < valPredRhs->loadI;
}

Remediator::RemedResp
LoadedValuePredRemediator::memdep(const Instruction *A, const Instruction *B,
                                  bool loopCarried, bool RAW, const Loop *L) {
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  std::shared_ptr<LoadedValuePredRemedy> remedy =
      std::shared_ptr<LoadedValuePredRemedy>(new LoadedValuePredRemedy());
  remedy->cost = DEFAULT_LOADED_VALUE_PRED_REMED_COST;

  // if A or B is a loop-invariant load instruction report no dep
  bool predA = predspec->isPredictable(A, L);
  bool predB = predspec->isPredictable(B, L);
  if (predA || predB) {
    ++numNoMemDep;
    remedy->loadI = (predA) ? dyn_cast<LoadInst>(A) : dyn_cast<LoadInst>(B);
    remedResp.depRes = DepResult::NoDep;

    DEBUG(errs() << "LoadedValuePredRemed removed mem dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
