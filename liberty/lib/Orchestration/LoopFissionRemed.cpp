#define DEBUG_TYPE "loop-fission-remed"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/LoopFissionRemed.h"

#define DEFAULT_LOOP_FISSION_REMED_COST 60

namespace liberty {
using namespace llvm;

STATISTIC(numLoopFissionNoRegDep, "Number of reg deps removed by loop fission");
STATISTIC(numLoopFissionNoCtrlDep,
          "Number of ctrl deps removed by loop fission");

void LoopFissionRemedy::apply(Task *task) {
  // TODO: code for application of loop fission here.
}

bool LoopFissionRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<LoopFissionRemedy> loopFissionRhs =
      std::static_pointer_cast<LoopFissionRemedy>(rhs);
  return this->produceI < loopFissionRhs->produceI;
}

bool isReplicable(const Instruction *I) { return !I->mayWriteToMemory(); }

bool LoopFissionRemediator::seqStageEligible(
    std::queue<const Instruction *> &instQ,
    std::unordered_set<const Instruction *> &visited, Criticisms &cr) {
  EdgeWeight seqStageWeight = 0;
  const Instruction *rootInst = instQ.front();
  while (!instQ.empty()) {
    const Instruction *inst = instQ.front();
    instQ.pop();

    if (visited.count(inst))
      continue;
    visited.insert(inst);

    if (notSeqStageEligible.count(inst))
      return false;

    if (seqStageEligibleInsts.count(inst))
      return true;

    // check if the sequential part is more than 5% of total loop weight
    seqStageWeight += perf.estimate_weight(inst);
    //if ((seqStageWeight * 100.0) / loopWeight >= 5.0) {
    if ((seqStageWeight * 100.0) / loopWeight >= 10.0) {
      notSeqStageEligible.insert(rootInst);
      return false;
    }

    if (!isReplicable(inst)) {
      notSeqStageEligible.insert(rootInst);
      return false;
    }

    auto pdgNode = pdg->fetchNode(const_cast<Instruction *>(inst));
    for (auto edge : pdgNode->getIncomingEdges()) {
      if (edge->isRemovableDependence())
        cr.insert(edge);
      else {
        auto outgoingV = edge->getOutgoingT();
        if (!pdg->isInternal(outgoingV))
          continue;
        Instruction *outgoingI = dyn_cast<Instruction>(outgoingV);
        assert(outgoingI && "pdg node is not an instruction");
        instQ.push(outgoingI);
      }
    }
  }
  for (auto &I :visited)
    seqStageEligibleInsts.insert(I);
  return true;
}

Remediator::RemedCriticResp
LoopFissionRemediator::removeDep(const Instruction *A, const Instruction *B,
                                 bool LoopCarried) {
  Remediator::RemedCriticResp remedResp;

  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<LoopFissionRemedy> remedy =
      std::shared_ptr<LoopFissionRemedy>(new LoopFissionRemedy());
  remedy->cost = DEFAULT_LOOP_FISSION_REMED_COST;

  std::queue<const Instruction *> instQ;
  instQ.push(A);
  InstSet_uptr visited = std::make_unique<InstSet>();
  Criticisms_uptr cr = std::make_unique<Criticisms>();

  if (!LoopCarried || !seqStageEligible(instQ, *visited, *cr)) {
    remedResp.remedy = remedy;
    return remedResp;
  }

  remedResp.depRes = DepResult::NoDep;
  //remedResp.criticisms = std::move(cr);
  remedy->produceI = A;
  //remedy->replicatedI = std::move(visited);

  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedCriticResp
LoopFissionRemediator::removeCtrldep(const Instruction *A, const Instruction *B,
                                     const Loop *L) {

  Remediator::RemedCriticResp remedResp = removeDep(A, B, true);

  if (remedResp.depRes == DepResult::NoDep) {
    ++numLoopFissionNoCtrlDep;
    DEBUG(errs() << "LoopFissionRemed removed ctrl dep between inst " << *A
                 << "  and  " << *B << '\n');
  }
  return remedResp;
}

Remediator::RemedCriticResp
LoopFissionRemediator::removeRegDep(const Instruction *A, const Instruction *B,
                                    bool loopCarried, const Loop *L) {

  Remediator::RemedCriticResp remedResp = removeDep(A, B, loopCarried);

  if (remedResp.depRes == DepResult::NoDep) {
    ++numLoopFissionNoRegDep;
    DEBUG(errs() << "LoopFissionRemed removed reg dep between inst " << *A
                 << "  and  " << *B << '\n');
  }
  return remedResp;
}

Remediator::RemedCriticResp
LoopFissionRemediator::satisfy(Loop *loop, const Criticism *cr) {
  Instruction *sop = dyn_cast<Instruction>(cr->getOutgoingT());
  Instruction *dop = dyn_cast<Instruction>(cr->getIncomingT());
  assert(sop && dop &&
         "PDG nodes that are part of criticims should be instructions");
  bool lc = cr->isLoopCarriedDependence();
  RemedCriticResp r;
  if (!lc || cr->isMemoryDependence())
    r.depRes = DepResult::Dep;
  else if (cr->isControlDependence())
    r = removeCtrldep(sop, dop, loop);
  else
    r = removeRegDep(sop, dop, lc, loop);

  return r;
}

} // namespace liberty
