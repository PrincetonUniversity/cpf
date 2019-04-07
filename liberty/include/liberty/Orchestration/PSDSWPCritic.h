#ifndef LLVM_LIBERTY_PS_DSWP_CRITIC_H
#define LLVM_LIBERTY_PS_DSWP_CRITIC_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Debug.h"

#include "liberty/GraphAlgorithms/EdmondsKarp.h"
#include "liberty/GraphAlgorithms/Graphs.h"
#include "liberty/Orchestration/Critic.h"
#include "liberty/Strategy/PerformanceEstimator.h"
#include "liberty/LoopProf/LoopProfLoad.h"

#include "PDG.hpp"
#include "SCC.hpp"
#include "SCCDAG.hpp"
#include "DGGraphTraits.hpp"
#include "LoopDependenceInfo.hpp"

#include <memory>
#include <queue>
#include <unordered_set>

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

class PSDSWPCritic : public Critic {
public:
  PSDSWPCritic(PerformanceEstimator *perf, unsigned threadBudget,
               LoopProfLoad *lpl)
      : Critic(perf, threadBudget, lpl) {}

  ~PSDSWPCritic() {
    delete optimisticPDG;
    delete optimisticSCCDAG;
  }

  CriticRes getCriticisms(PDG &pdg, Loop *loop, LoopDependenceInfo &ldi);

  StringRef getCriticName() const { return "ps-dswp-critic"; }

  void simplifyPDG(PDG *pdg);

  void populateCriticisms(PipelineStrategy &ps, Criticisms &criticisms,
                          PDG &pdg);

  bool doallAndPipeline(const PDG &pdg, const SCCDAG &sccdag,
                        SCCDAG::SCCSet &all_sccs, PerformanceEstimator &perf,
                        PipelineStrategy::Stages &stages, unsigned threadBudget,
                        bool includeReplicableStages,
                        bool includeParallelStages);

  bool doallAndPipeline(const PDG &pdg, const SCCDAG &sccdag,
                        PerformanceEstimator &perf,
                        PipelineStrategy::Stages &stages, unsigned threadBudget,
                        bool includeReplicableStages,
                        bool includeParallelStages);

private:
  Loop *loop;
  PDG *optimisticPDG;
  SCCDAG *optimisticSCCDAG;
};

} // namespace liberty

#endif
