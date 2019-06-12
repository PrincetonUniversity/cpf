#define DEBUG_TYPE "priv-remed"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/PrivRemed.h"

// conservative privitization in many cases is as expensive as memory versioning
// and locality private. Need to always keep track of who wrote last.
//#define DEFAULT_PRIV_REMED_COST 1
#define DEFAULT_PRIV_REMED_COST 100

namespace liberty {
using namespace llvm;

STATISTIC(numPrivNoMemDep,
          "Number of false mem deps removed by privitization");

void PrivRemedy::apply(Task *task) {
  this->task = task;
  replacePrivateLoadsStore((Instruction*)this->storeI);
}

bool PrivRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<PrivRemedy> privRhs =
      std::static_pointer_cast<PrivRemedy>(rhs);
  return this->storeI < privRhs->storeI;
}

// verify that noone from later iteration reads the written value by this store.
// conservatively ensure that the given store instruction is not part of any
// loop-carried memory flow (RAW) dependences
bool PrivRemediator::isPrivate(const Instruction *I) {
  auto pdgNode = pdg->fetchNode(const_cast<Instruction*>(I));
  for (auto edge : pdgNode->getOutgoingEdges()) {
    if (edge->isLoopCarriedDependence() && edge->isMemoryDependence() &&
        edge->isRAWDependence() && pdg->isInternal(edge->getIncomingT()))
      return false;
  }
  return true;
}

Remediator::RemedResp PrivRemediator::memdep(const Instruction *A,
                                             const Instruction *B,
                                             bool LoopCarried, bool RAW,
                                             const Loop *L) {

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<PrivRemedy> remedy =
      std::shared_ptr<PrivRemedy>(new PrivRemedy());
  remedy->cost = DEFAULT_PRIV_REMED_COST;

  // need to be loop-carried WAW or WAR where the privitizable store is the instruction B
  if (LoopCarried && isa<StoreInst>(B) && isPrivate(B)) {
    ++numPrivNoMemDep;
    remedResp.depRes = DepResult::NoDep;
    remedy->storeI = dyn_cast<StoreInst>(B);

    DEBUG(errs() << "PrivRemed removed mem dep between inst " << *A << "  and  "
                 << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
