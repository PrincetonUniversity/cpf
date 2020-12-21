#define DEBUG_TYPE "spice-remed"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Instructions.h"

#include "liberty/Orchestration/SpiceRemed.h"
#include "liberty/Utilities/IV.h"

#define DEFAULT_SPICE_REMED_COST 0

namespace liberty {
using namespace llvm;

STATISTIC(numNoRegDep, "Number of reg deps removed by Spice remed");
STATISTIC(numNoCtrlDep, "Number of ctrl deps removed by Spice remed");

void SpiceRemedy::apply(Task *task) {
}

bool SpiceRemedy::compare(const Remedy_ptr rhs) const {
  return false;
}

Remediator::RemedResp SpiceRemediator::regdep(const Instruction *A,
                                              const Instruction *B,
                                              bool loopCarried, const Loop *L){
  Remediator::RemedResp remedResp;
  remedResp.depRes = DepResult::Dep;
  auto remedy = std::make_shared<SpiceRemedy>();
  remedy->cost = DEFAULT_SPICE_REMED_COST;
  remedResp.remedy = remedy;
  double bPred = spice_profile.predictability(const_cast<Instruction*>(B));
  if(loopCarried && bPred > 0.80)
  {
    errs() << "Instruction A: \n" << *A << "\n";
    errs() << "Instruction B: \n" << *B << "\n";
    for(const Use &U : B->operands())
    {
      Value* uv = U.get();
      if(!isa<Instruction>(uv))
        continue;
      Instruction* uInst = dyn_cast<Instruction>(uv);
      if(uInst == A)
      {
        remedResp.depRes = DepResult::NoDep;
        break;
      }
    }
  }
  return remedResp;
}

Remediator::RemedResp SpiceRemediator::ctrldep(const Instruction *A,
                                                    const Instruction *B,
                                                    const Loop *L) {
  Remediator::RemedResp remedResp;
  remedResp.depRes = DepResult::Dep;
  auto remedy = std::make_shared<SpiceRemedy>();
  remedy->cost = DEFAULT_SPICE_REMED_COST;
  remedResp.remedy = remedy;
  //if(loopCarried)
  //{
    //errs() << "Instruction A: \n" << *A << "\n";
    //errs() << "Instruction B: \n" << *B << "\n";
  //}
  return remedResp;
}

Remediator::RemedResp SpiceRemediator::memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   DataDepType dataDepTy, const Loop *L){
  Remediator::RemedResp remedResp;
  remedResp.depRes = DepResult::Dep;
  auto remedy = std::make_shared<SpiceRemedy>();
  remedy->cost = DEFAULT_SPICE_REMED_COST;
  remedResp.remedy = remedy;
  //if(loopCarried)
  //{
    //errs() << "Instruction A: \n" << *A << "\n";
    //errs() << "Instruction B: \n" << *B << "\n";
  //}
  return remedResp;
}

Remedies SpiceRemediator::satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms){
    Remedies remedies = Remediator::satisfy(pdg, loop, criticisms);

    return remedies;
}

} // namespace liberty
