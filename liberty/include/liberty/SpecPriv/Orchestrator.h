#ifndef LLVM_LIBERTY_ORCHESTRATOR_H
#define LLVM_LIBERTY_ORCHESTRATOR_H

#include "liberty/SpecPriv/PipelineStrategy.h"
#include "liberty/SpecPriv/Remediator.h"
#include "liberty/SpecPriv/Critic.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/SpecPriv/TXIORemed.h"
#include "liberty/SpecPriv/ControlSpecRemed.h"
#include "liberty/SpecPriv/ReduxRemed.h"
#include "liberty/SpecPriv/SmtxSlampRemed.h"
#include "liberty/SpecPriv/HeaderPhiPredRemed.h"
//#include "liberty/SpecPriv/ReplicaRemed.h"
#include "liberty/SpecPriv/CommutativeLibsRemed.h"
//#include "liberty/SpecPriv/CommutativeGuessRemed.h"
//#include "liberty/SpecPriv/PureFunRemed.h"
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
typedef std::unique_ptr<Critic> Critic_ptr;

class Orchestrator {
public:
  bool findBestStrategy(
      // Inputs
      Loop *loop, llvm::PDG &pdg, LoopDependenceInfo &ldi,
      PerformanceEstimator &perf, ControlSpeculation *ctrlspec,
      PredictionSpeculation *headerPhiPred, ModuleLoops &mloops,
      SmtxSlampSpeculationManager &smtxMan, LoopProfLoad &lpl,
      // Output
      PipelineStrategy *strat, std::unique_ptr<SelectedRemedies> &sRemeds,
      // Optional inputs
      unsigned threadBudget = 25, bool ignoreAntiOutput = false,
      bool includeReplicableStages = true, bool constrainSubLoops = false,
      bool abortIfNoParallelStage = true);

private:
  std::map<Criticism*, Remedies> mapCriticismsToRemeds;
  std::map<u_sptr, Remedy_ptr> mapRemedEdgeCostsToRemedies;

  std::set<Remediator_ptr> getRemediators(Loop *A, ControlSpeculation *ctrlspec,
                                          PredictionSpeculation *headerPhiPred,
                                          ModuleLoops &mloops,
                                          LoopDependenceInfo &ldi,
                                          SmtxSlampSpeculationManager &smtxMan);

  std::set<Critic_ptr> getCritics(PerformanceEstimator *perf,
                                  unsigned threadBudget, LoopProfLoad *lpl);

  void addressCriticisms(SelectedRemedies &selectedRemedies,
                         long &selectedRemediesCost, Criticisms &criticisms);
};

} // namespace SpecPriv
} // namespace liberty

#endif
