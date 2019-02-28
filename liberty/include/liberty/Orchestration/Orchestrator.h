#ifndef LLVM_LIBERTY_ORCHESTRATOR_H
#define LLVM_LIBERTY_ORCHESTRATOR_H

#include "liberty/Strategy/PipelineStrategy.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Orchestration/Critic.h"
#include "liberty/Orchestration/TXIORemed.h"
#include "liberty/Orchestration/ControlSpecRemed.h"
#include "liberty/Orchestration/ReduxRemed.h"
#include "liberty/Orchestration/PrivRemed.h"
#include "liberty/Orchestration/SmtxSlampRemed.h"
#include "liberty/Orchestration/HeaderPhiPredRemed.h"
#include "liberty/Orchestration/CountedIVRemed.h"
//#include "liberty/Orchestration/ReplicaRemed.h"
#include "liberty/Orchestration/CommutativeLibsRemed.h"
//#include "liberty/Orchestration/CommutativeGuessRemed.h"
//#include "liberty/Orchestration/PureFunRemed.h"
#include "PDG.hpp"
#include "SCCDAG.hpp"
#include "LoopDependenceInfo.hpp"

#include <vector>
#include <memory>

namespace liberty {
namespace SpecPriv {
using namespace llvm;

struct PerformanceEstimator;

typedef std::vector<Remedy_ptr> SelectedRemedies;
typedef std::unique_ptr<Remediator> Remediator_ptr;
typedef std::shared_ptr<Critic> Critic_ptr;

class Orchestrator {
public:
  bool findBestStrategy(
      // Inputs
      Loop *loop, llvm::PDG &pdg, LoopDependenceInfo &ldi,
      PerformanceEstimator &perf, ControlSpeculation *ctrlspec,
      PredictionSpeculation *headerPhiPred, ModuleLoops &mloops,
      SmtxSlampSpeculationManager &smtxMan, LoopProfLoad &lpl,
      // Output
      std::unique_ptr<PipelineStrategy> &strat,
      std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
      // Optional inputs
      unsigned threadBudget = 25, bool ignoreAntiOutput = false,
      bool includeReplicableStages = true, bool constrainSubLoops = false,
      bool abortIfNoParallelStage = true);

private:
  std::map<Criticism*, Remedies> mapCriticismsToRemeds;
  std::map<u_sptr, Remedy_ptr> mapRemedEdgeCostsToRemedies;

  std::set<Remediator_ptr>
  getRemediators(Loop *A, PDG *pdg, ControlSpeculation *ctrlspec,
                 PredictionSpeculation *headerPhiPred, ModuleLoops &mloops,
                 LoopDependenceInfo &ldi, SmtxSlampSpeculationManager &smtxMan);

  std::set<Critic_ptr> getCritics(PerformanceEstimator *perf,
                                  unsigned threadBudget, LoopProfLoad *lpl);

  void addressCriticisms(SelectedRemedies &selectedRemedies,
                         long &selectedRemediesCost, Criticisms &criticisms);
};

} // namespace SpecPriv
} // namespace liberty

#endif
