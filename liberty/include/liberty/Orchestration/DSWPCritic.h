#ifndef LLVM_LIBERTY_DSWP_CRITIC_H
#define LLVM_LIBERTY_DSWP_CRITIC_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"

#include "liberty/GraphAlgorithms/EdmondsKarp.h"
#include "liberty/GraphAlgorithms/Graphs.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "scaf/SpeculationModules/ControlSpecRemed.h"
#include "liberty/Orchestration/Critic.h"
#include "scaf/SpeculationModules/ReduxRemed.h"
#include "liberty/Strategy/PerformanceEstimator.h"

#include "noelle/core/DGGraphTraits.hpp"
#include "noelle/core/LoopDependenceInfo.hpp"
#include "noelle/core/PDG.hpp"
#include "noelle/core/SCC.hpp"
#include "noelle/core/SCCDAG.hpp"

#include <memory>
#include <queue>
#include <unordered_set>
#include <vector>

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;
using namespace SpecPriv;

class DSWPCritic : public Critic {
public:
  DSWPCritic(PerformanceEstimator *perf, unsigned threadBudget,
               LoopProfLoad *lpl)
      : Critic(perf, threadBudget, lpl) {}

  StringRef getCriticName() const { return "dswp-critic"; }

  CriticRes getCriticisms(PDG &pdg, Loop *loop);

private:
  Loop *loop;
  map<SCC *, double> weightCache;

  PDG getOptimisticPdg(PDG &pdg);
  double getWeight(SCC *scc);
  std::map<unsigned, unsigned> getIncomingCountMap(SCCDAG &sccdag);
  SCC* getNextLargestFreeStandingSCC(SCCDAG &sccdag, std::map<unsigned, unsigned> &incomingCountMap);
  void updateIncomingCountMap(SCCDAG &sccdag, std::map<unsigned, unsigned> &incomingCountMap, SCC *removedScc);
  void populateCrossStageDependences(PipelineStrategy &ps, PDG &pdg);
};

} // namespace liberty

#endif
