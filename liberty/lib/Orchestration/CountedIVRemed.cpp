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
  auto livm = ldi->getInductionVariableManager();
  auto ls   = ldi->getLoopStructure();
  auto ivA   = livm->getInductionVariable(*ls, const_cast<Instruction*> (A));
  auto ivB   = livm->getInductionVariable(*ls, const_cast<Instruction*> (B));

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  if (!loopCarried)
    return remedResp;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = 0;

  if (ivA || ivB)
  {
    if(ivA)
      errs()<< "Susan: found IV inst A\n" << *A << "\n";
    if(ivB)
      errs()<< "Susan: found IV inst B\n" << *B << "\n";
    ++numNoRegDep;
    remedy->allIVInfo = livm;
    remedResp.depRes = DepResult::NoDep;
    auto PHIA = ivA->getLoopEntryPHI();
    auto PHIB = ivB->getLoopEntryPHI();
    if(PHIA == PHIB)
      remedy->ivPHI = PHIA;
    else
    {
      assert(!(PHIA && PHIB) && "assuming not both instructiosn are IV instructions");
      if(PHIA)
        remedy->ivPHI = PHIA;
      else if(PHIB)
        remedy->ivPHI = PHIB;
    }
  }

  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedResp CountedIVRemediator::ctrldep(const Instruction *A,
                                                   const Instruction *B, const Loop *loop){
  auto livm = ldi->getInductionVariableManager();
  auto ls   = ldi->getLoopStructure();
  auto term = (ls->getHeader())->getTerminator();

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  auto remedy = std::make_shared<CountedIVRemedy>();
  remedy->cost = 0;

  if(term == B || term == A)
  {
    if(auto cmp_inst = dyn_cast<CmpInst>(term->getOperand(0)))
    {
      for (auto op = cmp_inst->op_begin(); op != cmp_inst->op_end(); ++op)
      {
        auto iv_inst = dyn_cast<Instruction> (op);
        auto iv = livm->getInductionVariable(*ls, iv_inst);
        if(iv)
        {
          ++numNoCtrlDep;
          remedy->ivPHI = iv->getLoopEntryPHI();
          remedResp.depRes = DepResult::NoDep;
        }
      }
    }
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
