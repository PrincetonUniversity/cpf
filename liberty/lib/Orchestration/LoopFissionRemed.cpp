#define DEBUG_TYPE "loop-fission-remed"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/LoopFissionRemed.h"

#define DEFAULT_LOOP_FISSION_REMED_COST 60

namespace liberty {
using namespace llvm;

STATISTIC(numLoopFissionNoRegDep, "Number of reg deps removed by loop fission");
STATISTIC(numLoopFissionNoCtrlDep, "Number of ctrl deps removed by loop fission");

void LoopFissionRemedy::apply(PDG &pdg) {
  // TODO: code for application of loop fission here.
}

bool LoopFissionRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<LoopFissionRemedy> loopFissionRhs =
      std::static_pointer_cast<LoopFissionRemedy>(rhs);
  return this->seqSCC < loopFissionRhs->seqSCC;
}

bool LoopFissionRemediator::isReplicable(SCC *scc) {
  if (replicableSCCs.count(scc))
    return true;

  if (nonReplicableSCCs.count(scc))
    return false;

  for (auto instPair : scc->internalNodePairs()) {
    const Instruction *inst = dyn_cast<Instruction>(instPair.first);
    assert(inst);

    if (inst->mayWriteToMemory()) {
      nonReplicableSCCs.insert(scc);
      return false;
    }
  }
  replicableSCCs.insert(scc);
  return true;
}

bool LoopFissionRemediator::seqStageEligible(
    SCCDAG *sccdag, std::queue<SCC *> sccQ, std::unordered_set<SCC *> visited) {
  while (!sccQ.empty()) {
    SCC *scc = sccQ.front();
    sccQ.pop();

    if (visited.count(scc))
      continue;
    visited.insert(scc);

    if (!isReplicable(scc))
      return false;

    auto sccNode = sccdag->fetchNode(scc);
    for (auto edge : sccNode->getIncomingEdges()) {
      auto outgoingSCC = edge->getOutgoingT();
      sccQ.push(outgoingSCC);
    }
  }
  return true;
}

Remediator::RemedResp LoopFissionRemediator::removeDep(const Instruction *A,
                                                       const Instruction *B,
                                                       bool LoopCarried) {
  Remediator::RemedResp remedResp;

  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<LoopFissionRemedy> remedy =
      std::shared_ptr<LoopFissionRemedy>(new LoopFissionRemedy());
  remedy->cost = DEFAULT_LOOP_FISSION_REMED_COST;

  auto sccdag = loopDepInfo->loopSCCDAG;
  auto bSCC = sccdag->sccOfValue(const_cast<Instruction *>(B));

  std::queue<SCC *> sccQ;
  sccQ.push(bSCC);
  std::unordered_set<SCC *> visited;

  if (!LoopCarried || !seqStageEligible(sccdag, sccQ, visited)) {
    remedResp.remedy = remedy;
    return remedResp;
  }

  remedResp.depRes = DepResult::NoDep;
  remedy->seqSCC = bSCC;

  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedResp LoopFissionRemediator::ctrldep(const Instruction *A,
                                                     const Instruction *B,
                                                     const Loop *L) {

  Remediator::RemedResp remedResp = removeDep(A, B, true);

  if (remedResp.depRes == DepResult::NoDep) {
    ++numLoopFissionNoCtrlDep;
    DEBUG(errs() << "LoopFissionRemed removed ctrl dep between inst " << *A
                 << "  and  " << *B << '\n');
  }
  return remedResp;
}

Remediator::RemedResp LoopFissionRemediator::regdep(const Instruction *A,
                                                    const Instruction *B,
                                                    bool loopCarried,
                                                    const Loop *L) {

  Remediator::RemedResp remedResp = removeDep(A, B, loopCarried);

  if (remedResp.depRes == DepResult::NoDep) {
    ++numLoopFissionNoRegDep;
    DEBUG(errs() << "LoopFissionRemed removed reg dep between inst " << *A
                 << "  and  " << *B << '\n');
  }
  return remedResp;
}

} // namespace liberty
