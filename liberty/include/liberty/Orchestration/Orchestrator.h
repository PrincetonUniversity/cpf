#ifndef LLVM_LIBERTY_ORCHESTRATOR_H
#define LLVM_LIBERTY_ORCHESTRATOR_H

#include "LoopDependenceInfo.hpp"
#include "PDG.hpp"
#include "SCCDAG.hpp"
#include "scaf/MemoryAnalysisModules/KillFlow.h"
#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/Orchestration/CommutativeLibsAA.h"
#include "liberty/Orchestration/ControlSpecRemed.h"
#include "liberty/Orchestration/CountedIVRemed.h"
#include "liberty/Orchestration/Critic.h"
#include "liberty/Orchestration/LocalityAA.h"
#include "liberty/Orchestration/MemSpecAARemed.h"
#include "liberty/Orchestration/MemVerRemed.h"
#include "liberty/Orchestration/PSDSWPCritic.h"
#include "liberty/Orchestration/ReduxRemed.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Orchestration/SmtxAA.h"
#include "liberty/Speculation/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Strategy/PipelineStrategy.h"
#include "scaf/Utilities/PrintDebugInfo.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace liberty {
namespace SpecPriv {
using namespace llvm;
using namespace llvm::noelle;

struct PerformanceEstimator;

typedef std::set<Remedy_ptr, RemedyCompare> SelectedRemedies;
typedef std::unique_ptr<Remediator> Remediator_ptr;
typedef std::shared_ptr<Critic> Critic_ptr;

class Orchestrator {
public:
  bool findBestStrategy(
      // Inputs
      Loop *loop, llvm::noelle::PDG &pdg, // LoopDependenceInfo &ldi,
      PerformanceEstimator &perf, ControlSpeculation *ctrlspec,
      PredictionSpeculation *loadedValuePred, ModuleLoops &mloops,
      TargetLibraryInfo *tli, SmtxSpeculationManager &smtxLampMan,
      PtrResidueSpeculationManager &ptrResMan, LAMPLoadProfile &lamp,
      const Read &rd, const HeapAssignment &asgn, Pass &proxy, LoopAA *loopAA,
      KillFlow &kill, KillFlow_CtrlSpecAware *killflowA,
      CallsiteDepthCombinator_CtrlSpecAware *callsiteA, LoopProfLoad &lpl,
      // Output
      std::unique_ptr<PipelineStrategy> &strat,
      std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
      // Optional inputs
      unsigned threadBudget = 25, bool ignoreAntiOutput = false,
      bool includeReplicableStages = true, bool constrainSubLoops = false,
      bool abortIfNoParallelStage = true);

  bool findBestStrategyGivenBestPDG(
      Loop *loop, llvm::noelle::PDG &pdg, PerformanceEstimator &perf,
      LoopProfLoad &lpl, std::unique_ptr<PipelineStrategy> &strat,
      std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
      // Optional inputs
      unsigned threadBudget = 25, bool ignoreAntiOutput = false,
      bool includeReplicableStages = true, bool constrainSubLoops = false,
      bool abortIfNoParallelStage = true);

private:
  std::vector<Remediator_ptr>
  getRemediators(Loop *A, PDG *pdg, ControlSpeculation *ctrlspec,
                 PredictionSpeculation *loadedValuePred, ModuleLoops &mloops,
                 TargetLibraryInfo *tli, // LoopDependenceInfo &ldi,
                 SmtxSpeculationManager &smtxLampMan,
                 PtrResidueSpeculationManager &ptrResMan, LAMPLoadProfile &lamp,
                 const Read &rd, const HeapAssignment &asgn, Pass &proxy,
                 LoopAA *loopAA, KillFlow &kill,
                 KillFlow_CtrlSpecAware *killflowA,
                 CallsiteDepthCombinator_CtrlSpecAware *callsiteA,
                 PerformanceEstimator *perf);

  std::vector<Critic_ptr> getCritics(PerformanceEstimator *perf,
                                     unsigned threadBudget, LoopProfLoad *lpl);

  void addressCriticisms(SelectedRemedies &selectedRemedies,
                         unsigned long &selectedRemediesCost,
                         Criticisms &criticisms);

  void printRemediatorSelectionCnt();
  void printRemedies(Remedies &rs, bool selected);
  void printSelected(const SetOfRemedies &sors, const Remedies_ptr &selected,
                     Criticism &cr);
  void printAllRemedies(const SetOfRemedies &sors, Criticism &cr);

  unordered_map<std::string, unsigned> remediatorSelectionCnt;
};

} // namespace SpecPriv
} // namespace liberty

#endif
