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
#include "liberty/Orchestration/ReduxRemed.h"
#include "liberty/Orchestration/TXIORemed.h"

#include "PDG.hpp"
#include "SCC.hpp"
#include "SCCDAG.hpp"
#include "DGGraphTraits.hpp"
#include "LoopDependenceInfo.hpp"

#include <memory>
#include <queue>
#include <unordered_set>
#include <vector>

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

  StringRef getCriticName() const { return "ps-dswp-critic"; }

  CriticRes getCriticisms(PDG &pdg, Loop *loop, LoopDependenceInfo &ldi);

  void critForPipelineProperty(const PDG &pdg, const PipelineStage &earlyStage,
                               const PipelineStage &lateStage,
                               Criticisms &criticisms, PipelineStrategy &ps);

  void critForParallelStageProperty(const PDG &pdg,
                                    const PipelineStage &parallel,
                                    Criticisms &criticisms,
                                    PipelineStrategy &ps);

  unsigned long moveOffStage(const PDG &pdg, Instruction *inst,
                             unordered_set<Instruction *> &visited,
                             set<Instruction *> *instsTgtSeq,
                             unordered_set<Instruction *> &instsMovedTgtSeq,
                             unordered_set<Instruction *> &instsMovedOtherSeq,
                             set<Instruction *> *instsOtherSeq,
                             unordered_set<DGEdge<Value> *> &edgesNotRemoved,
                             const EdgeWeight curOffPStageWeight,
                             bool moveToFront);

  bool avoidElimDep(const PDG &pdg, PipelineStrategy &ps, DGEdge<Value> *edge,
                    unordered_set<Instruction *> &instsMovedToFront,
                    unordered_set<Instruction *> &instsMovedToBack,
                    unordered_set<DGEdge<Value> *> &edgesNotRemoved);

  EdgeWeight getParalleStageWeight(PipelineStrategy &ps);

  void adjustPipeline(PipelineStrategy &ps, PDG &pdg);

  void populateCriticisms(PipelineStrategy &ps, Criticisms &criticisms,
                          PDG &pdg);

  void simplifyPDG(PDG *pdg);

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

  // dependence graphs after removing all the removable dependences
  PDG *optimisticPDG;
  SCCDAG *optimisticSCCDAG;

  // insts that cannot be moved to the first seq stage
  unordered_set<Instruction *> notMovableInstsToFront;

  // insts that cannot be moved to the last seq stage
  unordered_set<Instruction *> notMovableInstsToBack;

  // original parallel stage weight after partitioning and before any movement
  EdgeWeight parallelStageWeight;

  // keeps track of weight of insts moved off the parallel stage
  EdgeWeight offPStageWeight;
};

} // namespace liberty

#endif
