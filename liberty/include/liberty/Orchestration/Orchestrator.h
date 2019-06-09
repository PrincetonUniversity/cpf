#ifndef LLVM_LIBERTY_ORCHESTRATOR_H
#define LLVM_LIBERTY_ORCHESTRATOR_H

#include "liberty/Strategy/PipelineStrategy.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/Utilities/PrintDebugInfo.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Orchestration/Critic.h"
#include "liberty/Orchestration/PSDSWPCritic.h"
#include "liberty/Orchestration/TXIORemed.h"
#include "liberty/Orchestration/ControlSpecRemed.h"
#include "liberty/Orchestration/ReduxRemed.h"
#include "liberty/Orchestration/PrivRemed.h"
#include "liberty/Orchestration/SmtxSlampRemed.h"
#include "liberty/Orchestration/SmtxLampRemed.h"
#include "liberty/Orchestration/HeaderPhiPredRemed.h"
#include "liberty/Orchestration/LoadedValuePredRemed.h"
#include "liberty/Orchestration/CountedIVRemed.h"
#include "liberty/Orchestration/LocalityRemed.h"
#include "liberty/Orchestration/LocalityAA.h"
#include "liberty/Orchestration/MemVerRemed.h"
#include "liberty/Orchestration/LoopFissionRemed.h"
//#include "liberty/Orchestration/ReplicaRemed.h"
#include "liberty/Orchestration/CommutativeLibsRemed.h"
//#include "liberty/Orchestration/CommutativeGuessRemed.h"
//#include "liberty/Orchestration/PureFunRemed.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Orchestration/SmtxAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "PDG.hpp"
#include "SCCDAG.hpp"
#include "LoopDependenceInfo.hpp"

#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>

namespace liberty {
namespace SpecPriv {
using namespace llvm;

struct PerformanceEstimator;

typedef std::set<Remedy_ptr, RemedyCompare> SelectedRemedies;
typedef std::unique_ptr<Remediator> Remediator_ptr;
typedef std::shared_ptr<Critic> Critic_ptr;

class Orchestrator {
public:
  bool findBestStrategy(
      // Inputs
      Loop *loop, llvm::PDG &pdg, LoopDependenceInfo &ldi,
      PerformanceEstimator &perf, ControlSpeculation *ctrlspec,
      PredictionSpeculation *loadedValuePred,
      PredictionSpeculation *headerPhiPred, ModuleLoops &mloops,
      SmtxSlampSpeculationManager &smtxMan, SmtxSpeculationManager &smtxLampMan,
      const Read &rd, const HeapAssignment &asgn, Pass &proxy, LoopAA *loopAA,
      LoopProfLoad &lpl,
      // Output
      std::unique_ptr<PipelineStrategy> &strat,
      std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
      // Optional inputs
      unsigned threadBudget = 25, bool ignoreAntiOutput = false,
      bool includeReplicableStages = true, bool constrainSubLoops = false,
      bool abortIfNoParallelStage = true);

private:
  std::map<Criticism*, SetOfRemedies> mapCriticismsToRemeds;

  std::vector<Remediator_ptr>
  getRemediators(Loop *A, PDG *pdg, ControlSpeculation *ctrlspec,
                 PredictionSpeculation *loadedValuePred,
                 PredictionSpeculation *headerPhiPred, ModuleLoops &mloops,
                 LoopDependenceInfo &ldi, SmtxSlampSpeculationManager &smtxMan,
                 SmtxSpeculationManager &smtxLampMan, const Read &rd,
                 const HeapAssignment &asgn, Pass &proxy, LoopAA *loopAA);

  std::vector<Critic_ptr> getCritics(PerformanceEstimator *perf,
                                     unsigned threadBudget, LoopProfLoad *lpl);

  void addressCriticisms(SelectedRemedies &selectedRemedies,
                         unsigned long &selectedRemediesCost,
                         Criticisms &criticisms);

  void printRemediatorSelectionCnt();
  void printRemedies(Remedies &rs, bool selected);
  void printSelected(SetOfRemedies &sors, const Remedies_ptr &selected,
                     Criticism &cr);

  unordered_map<std::string, unsigned> remediatorSelectionCnt;

};

} // namespace SpecPriv
} // namespace liberty

#endif
