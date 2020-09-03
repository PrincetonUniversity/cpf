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
                                                  bool loopCarried, const Loop *loop){
  //add a check to unconditionally branch to ON header iff there is a countedIV remed other wise just chunking without
  //To disprove the loop carried dependence from instruction A, a
  //stepping instruction and instruction B, the IV PHI
  auto livm = ldi->getInductionVariableManager();
  auto ls   = ldi->getLoopStructure();
  auto ivs   = livm->getInductionVariables(*ls);
  auto govern_iv = livm->getLoopGoverningInductionVariable (*ls);

  Remediator::RemedResp remedResp;
  remedResp.depRes = DepResult::Dep;

  if(!loopCarried || ivs.size() != 1 || govern_iv == NULL)
    return remedResp;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = 0;

  auto ivB   = livm->getInductionVariable(*ls, const_cast<Instruction*> (B));
  if(ivB == govern_iv)
  {
    auto insts = ivB->getNonPHIIntermediateValues();
    for (auto inst : insts)
    {
      if(inst == A)
      {
        ++numNoRegDep;
        remedy->allIVInfo = livm;
        remedResp.depRes = DepResult::NoDep;
				assert(ivB->getLoopEntryPHI() && "cannot find entry phi for governing IV");
        auto PHIB = ivB->getLoopEntryPHI();
        remedy->ivPHI = PHIB;
        remedy->IV    = ivB;
        break;
      }
    }
  }

  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedResp CountedIVRemediator::ctrldep(const Instruction *A,
                                                   const Instruction *B, const Loop *loop){
  auto livm      = ldi->getInductionVariableManager();
  auto ls        = ldi->getLoopStructure();
  auto LDG       = ldi->getLoopDG();
  Remediator::RemedResp remedResp;
  remedResp.depRes = DepResult::Dep;

  auto govern_attr    = livm->getLoopGoverningIVAttribution (*ls);
  BranchInst* iv_br   = govern_attr->getHeaderBrInst();
  InductionVariable* govern_iv = livm->getLoopGoverningInductionVariable(*ls);
  if(iv_br == NULL || cast<Instruction>(iv_br) != A || govern_iv == NULL)
    return remedResp;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = 0;

  bool BdepA = false;
  auto isCtrlDepOnA = [B, &BdepA](Value *to, DataDependenceType ddType) -> bool {
    if (!isa<Instruction>(to))
      return false;
    auto i = cast<Instruction>(to);
    if(B == i)
      BdepA = true;
    return false;
  };

	//find control dependent instructions on the IV branch inst
  LDG->iterateOverDependencesFrom(cast<Value>(iv_br), true, false, false, isCtrlDepOnA);
  if(BdepA)
  {
    ++numNoCtrlDep;
    remedy->allIVInfo = livm;
    remedResp.depRes = DepResult::NoDep;
		assert(govern_iv->getLoopEntryPHI() && "cannot find entry phi for governing IV");
    remedy->ivPHI = govern_iv->getLoopEntryPHI();
    remedy->IV = govern_iv;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
