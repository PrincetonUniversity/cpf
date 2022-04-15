#ifndef LLVM_LIBERTY_ORCHESTRATOR_H
#define LLVM_LIBERTY_ORCHESTRATOR_H

#include "noelle/core/LoopDependenceInfo.hpp"
#include "noelle/core/PDG.hpp"
#include "noelle/core/SCCDAG.hpp"
#include "scaf/SpeculationModules/PredictionSpeculation.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/PtrResidueManager.h"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "scaf/MemoryAnalysisModules/KillFlow.h"
#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "scaf/SpeculationModules/CommutativeLibsAA.h"
#include "scaf/SpeculationModules/ControlSpecRemed.h"
#include "scaf/SpeculationModules/CountedIVRemed.h"
#include "liberty/Orchestration/Critic.h"
#include "scaf/SpeculationModules/LocalityAA.h"
#include "scaf/SpeculationModules/MemVerRemed.h"
#include "liberty/Orchestration/PSDSWPCritic.h"
#include "liberty/Orchestration/DSWPCritic.h"
#include "scaf/SpeculationModules/ReduxRemed.h"
#include "scaf/SpeculationModules/Remediator.h"
#include "scaf/SpeculationModules/SmtxAA.h"
#include "liberty/Speculation/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Strategy/PipelineStrategy.h"

#include "scaf/SpeculationModules/MemSpecAARemed.h"
#include "scaf/SpeculationModules/GlobalConfig.h"
#include "scaf/Utilities/ControlSpeculation.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/Utilities/PrintDebugInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

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
  Orchestrator(Pass &proxy_): proxy(proxy_) {
      loopAA = proxy.getAnalysis<LoopAA>().getTopAA();
      lpl = &proxy.getAnalysis< LoopProfLoad >();
      perf = &proxy.getAnalysis< ProfilePerformanceEstimator >();
      mloops = &proxy.getAnalysis< ModuleLoops >();
      //kill = &proxy.getAnalysis< KillFlow >();

      // ctrl spec
      if (EnableEdgeProf) {
        ctrlspec = proxy.getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
        callsiteA = &proxy.getAnalysis<CallsiteDepthCombinator_CtrlSpecAware>();
        killflowA = &proxy.getAnalysis<KillFlow_CtrlSpecAware>();
        killflowA->setLoopOfInterest(nullptr, nullptr);
      }
      else {
        ctrlspec = nullptr;
        callsiteA = nullptr;
        killflowA = nullptr;
      }

      // SpecPriv
      if (EnableSpecPriv) {
        rd = &proxy.getAnalysis<ReadPass>().getProfileInfo();
        classify = &proxy.getAnalysis<Classify>();
        ptrResMan = &proxy.getAnalysis<PtrResidueSpeculationManager>();
        loadedValuePred = &proxy.getAnalysis<ProfileGuidedPredictionSpeculator>();
      }
      else {
        rd = nullptr;
        Classify *classify = nullptr;
        ptrResMan = nullptr;
        loadedValuePred = nullptr;
      }

      // LAMP
      if (EnableLamp) {
        smtxLampMan = &proxy.getAnalysis<SmtxSpeculationManager>();
        lamp = &proxy.getAnalysis<LAMPLoadProfile>();
      }
      else {
        smtxLampMan = nullptr;
        lamp = nullptr;
      }
  }

  bool findBestStrategy(Loop *loop, llvm::noelle::PDG &pdg,
      // Output
      std::unique_ptr<PipelineStrategy> &strat,
      std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
      // Optional inputs
      unsigned threadBudget = 25, bool ignoreAntiOutput = false,
      bool includeReplicableStages = true, bool constrainSubLoops = false,
      bool abortIfNoParallelStage = true);

/*
 *  bool findBestStrategy(
 *      // Inputs
 *      Loop *loop, llvm::noelle::PDG &pdg, // LoopDependenceInfo &ldi,
 *      PerformanceEstimator &perf, ControlSpeculation *ctrlspec,
 *      PredictionSpeculation *loadedValuePred, ModuleLoops &mloops,
 *      TargetLibraryInfo *tli, SmtxSpeculationManager &smtxLampMan,
 *      PtrResidueSpeculationManager &ptrResMan, LAMPLoadProfile &lamp,
 *      const Read &rd, const HeapAssignment &asgn, Pass &proxy, LoopAA *loopAA,
 *      KillFlow &kill, KillFlow_CtrlSpecAware *killflowA,
 *      CallsiteDepthCombinator_CtrlSpecAware *callsiteA, LoopProfLoad &lpl,
 *      // Output
 *      std::unique_ptr<PipelineStrategy> &strat,
 *      std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
 *      // Optional inputs
 *      unsigned threadBudget = 25, bool ignoreAntiOutput = false,
 *      bool includeReplicableStages = true, bool constrainSubLoops = false,
 *      bool abortIfNoParallelStage = true);
 *
 *  bool findBestStrategyGivenBestPDG(
 *      Loop *loop, llvm::noelle::PDG &pdg, PerformanceEstimator &perf, ControlSpeculation *ctrlspec, ModuleLoops &mloops,
 *      LoopProfLoad &lpl, LoopAA *loopAA, 
 *      const Read &rd, const HeapAssignment &asgn, Pass &proxy,
 *      std::unique_ptr<PipelineStrategy> &strat,
 *      std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
 *      // Optional inputs
 *      unsigned threadBudget = 25, bool ignoreAntiOutput = false,
 *      bool includeReplicableStages = true, bool constrainSubLoops = false,
 *      bool abortIfNoParallelStage = true);
 */

private:
  /*
   *std::vector<Remediator_ptr> getNonSpecRemediators(
   *    Loop *A, PDG *pdg, ControlSpeculation *ctrlspec, ModuleLoops &mloops, LoopAA *loopAA,
   *  const Read &rd, const HeapAssignment &asgn, Pass &proxy, PerformanceEstimator *perf);
   */

  Pass &proxy;
  LoopAA *loopAA;
  ModuleLoops *mloops;
  LoopProfLoad *lpl;
  PerformanceEstimator *perf;
  //KillFlow *kill;


  //TargetLibraryInfo *tli;

  // control speculation
  ControlSpeculation *ctrlspec;
  KillFlow_CtrlSpecAware *killflowA;
  CallsiteDepthCombinator_CtrlSpecAware *callsiteA;
  
  // SpecPriv
  Read *rd;
  Classify *classify;
  PtrResidueSpeculationManager *ptrResMan;
  PredictionSpeculation *loadedValuePred;
  
  // LAMP
  LAMPLoadProfile *lamp;
  SmtxSpeculationManager *smtxLampMan;

  /*
   *std::vector<Remediator_ptr>
   *getRemediators(Loop *A, PDG *pdg, ControlSpeculation *ctrlspec,
   *               PredictionSpeculation *loadedValuePred, ModuleLoops &mloops,
   *               TargetLibraryInfo *tli, // LoopDependenceInfo &ldi,
   *               SmtxSpeculationManager &smtxLampMan,
   *               PtrResidueSpeculationManager &ptrResMan, LAMPLoadProfile &lamp,
   *               const Read &rd, const HeapAssignment &asgn, Pass &proxy,
   *               LoopAA *loopAA, KillFlow &kill,
   *               KillFlow_CtrlSpecAware *killflowA,
   *               CallsiteDepthCombinator_CtrlSpecAware *callsiteA,
   *               PerformanceEstimator *perf);
   */

  std::vector<Remediator_ptr> getAvailableRemediators(Loop *A, PDG *pdg);

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
