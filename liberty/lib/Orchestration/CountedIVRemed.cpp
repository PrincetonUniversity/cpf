#define DEBUG_TYPE "counted-iv-remed"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Instructions.h"

#include "liberty/Orchestration/CountedIVRemed.h"
#include "liberty/Utilities/IV.h"

#define DEFAULT_COUNTED_IV_REMED_COST 0

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;

STATISTIC(numNoRegDep, "Number of reg deps removed by counted IV remed");
STATISTIC(numNoCtrlDep, "Number of ctrl deps removed by counted IV remed");

void CountedIVRemedy::apply(Task *task) {
}

bool CountedIVRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<CountedIVRemedy> countedIVRhs =
      std::static_pointer_cast<CountedIVRemedy>(rhs);
  return this->ivPHI < countedIVRhs->ivPHI;
}

Remediator::RemedResp CountedIVRemediator::regdep(const Instruction *A,
                                                  const Instruction *B,
                                                  bool loopCarried){
  auto livm = ldi->getInductionVariableManager();
  auto ls   = ldi->getLoopStructure();
  auto iv   = livm->getInductionVariable(*ls, const_cast<Instruction*> (B));

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  if (!loopCarried)
    return remedResp;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = 0;

  if (iv) {
    ++numNoRegDep;
    remedy->ivPHI = iv->getLoopEntryPHI();
    remedResp.depRes = DepResult::NoDep;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedResp CountedIVRemediator::ctrldep(const Instruction *A,
                                                   const Instruction *B){
  auto livm = ldi->getInductionVariableManager();
  auto ls   = ldi->getLoopStructure();
  auto iv   = livm->getInductionVariable(*ls, const_cast<Instruction*> (B));

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = 0;

  // remove all ctrl edges originating from branch controlled by a bounded IV
  if (iv) {
    ++numNoCtrlDep;
    remedy->ivPHI = iv->getLoopEntryPHI();
    remedResp.depRes = DepResult::NoDep;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
