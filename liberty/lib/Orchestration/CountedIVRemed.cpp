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
                                                  bool loopCarried,
                                                  const Loop *L) {
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  if (!loopCarried)
    return remedResp;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = 0;

  Function* f = L->getHeader()->getParent();
  ScalarEvolution *SE = &mLoop->getAnalysis_ScalarEvolution(f);
  PHINode* phiIV = liberty::getInductionVariable(L, *SE);
  Instruction* casted_phiIV;

  if ( phiIV && (casted_phiIV = dyn_cast<Instruction>(phiIV)) &&
       (B->isIdenticalTo(casted_phiIV)) ) {
    ++numNoRegDep;
    assert(casted_phiIV && "Dest inst in CountedIVRemediator::regdep not a phiNode??");
    remedy->ivPHI = phiIV;
    remedResp.depRes = DepResult::NoDep;
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
  remedy->cost = 0;

  PHINode* phiCIV = L->getCanonicalInductionVariable();
  Function* f = L->getHeader()->getParent();
  ScalarEvolution *SE = &mLoop->getAnalysis_ScalarEvolution(f);
  PHINode* phiIV = liberty::getInductionVariable(L,*SE);
  Instruction* casted_phiIV;

  // remove all ctrl edges originating from branch controlled by a bounded IV
  if ( phiIV && (casted_phiIV = dyn_cast<Instruction>(phiIV)) &&
       (B->isIdenticalTo(casted_phiIV))  ){
    ++numNoCtrlDep;
    remedResp.depRes = DepResult::NoDep;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
