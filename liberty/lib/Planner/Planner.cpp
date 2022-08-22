#include <memory>

#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Planner/Planner.h"
#include "liberty/Orchestration/Orchestrator.h"
#include "noelle/core/Noelle.hpp"
#include "scaf/Utilities/ReportDump.h"
#include "scaf/SpeculationModules/SLAMPLoad.h"
#include "scaf/SpeculationModules/SlampOracleAA.h"

namespace liberty {
  using namespace llvm::noelle;
  using namespace llvm;
  using namespace liberty::slamp;

  void Planner::getAnalysisUsage(AnalysisUsage &au) const {
    au.addRequired<LoopProfLoad>();
    au.addRequired<Noelle>();
    au.addRequired<TargetLibraryInfoWrapperPass>();
    au.addRequired<LoopInfoWrapperPass>();
    au.addRequired< LoopAA >();
    au.addRequired<PostDominatorTreeWrapperPass>();
    // au.addRequired<LLVMAAResults>();

    if (EnableEdgeProf) {
      au.addRequired<ProfileGuidedControlSpeculator>();
      // au.addRequired<KillFlow_CtrlSpecAware>();
      // au.addRequired<CallsiteDepthCombinator_CtrlSpecAware>();
    }

    if (EnableLamp) {
      au.addRequired<LAMPLoadProfile>();
      au.addRequired<SmtxSpeculationManager>();
    }

    if (EnableSlamp) {
      au.addRequired<SLAMPLoadProfile>();
      au.addRequired<SlampOracleAA>();
    }

    if (EnableSpecPriv) {
      au.addRequired<ProfileGuidedPredictionSpeculator>();
      au.addRequired<PtrResidueSpeculationManager>();
      au.addRequired<ReadPass>();
      au.addRequired<Classify>();
    }

    au.addRequired< ProfilePerformanceEstimator >();
    au.addRequired< Targets >();
    au.addRequired< ModuleLoops >();
    // add all speculative analyses
    au.setPreservesAll();
  }

  // intialize additional remedieators
  // these remediators should not be on the SCAF AA stack
  std::vector<Remediator_ptr> Planner::getAvailableRemediators(Loop *A, PDG *pdg) {
    std::vector<Remediator_ptr> remeds;

    /* produce remedies for control deps */
    if (EnableEdgeProf) {
      auto ctrlspec =
          getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
      ctrlspec->setLoopOfInterest(A->getHeader());
      auto ctrlSpecRemed = std::make_unique<ControlSpecRemediator>(ctrlspec);
      ctrlSpecRemed->processLoopOfInterest(A);
      remeds.push_back(std::move(ctrlSpecRemed));
    }

    /* produce remedies for register deps */
    // reduction remediator
    auto mloops = &getAnalysis<ModuleLoops>();
    auto aa = &getAnalysis<LoopAA>();

    auto reduxRemed = std::make_unique<ReduxRemediator>(mloops, aa, pdg);
    reduxRemed->setLoopOfInterest(A);
    remeds.push_back(std::move(reduxRemed));

    // counted induction variable remediator
    // disable IV remediator for PS-DSWP for now, handle it via replicable stage
    //remeds.push_back(std::make_unique<CountedIVRemediator>(&ldi));

    //// FIXME: don't need this for now
    // if (EnableSpecPriv && EnableLamp && EnableEdgeProf) {
    //   // full speculative analysis stack combining lamp, ctrl spec, value prediction,
    //   // points-to spec, separation logic spec, txio, commlibsaa, ptr-residue and
    //   // static analysis.
    //   const DataLayout &DL = A->getHeader()->getModule()->getDataLayout();
    //   const Ctx *ctx = rd->getCtx(A);
    //   const HeapAssignment &asgn = classify->getAssignmentFor(A);
    //   remeds.push_back(std::make_unique<MemSpecAARemediator>(
    //         proxy, ctrlspec, lamp, *rd, asgn, loadedValuePred, smtxLampMan,
    //         ptrResMan, killflowA, callsiteA, *mloops, perf));
    // }

    // memory versioning remediator (used separately from the rest since it cannot
    // collaborate with analysis modules. It cannot answer modref or alias
    // queries. Only addresses dependence queries about false deps)
    remeds.push_back(std::make_unique<MemVerRemediator>());

    return remeds;
  }

  vector<LoopAA *> Planner::addAndSetupSpecModulesToLoopAA(Module &M,
                                                           Loop *loop) {
    auto DL = &M.getDataLayout();
    PerformanceEstimator *perf = &getAnalysis<ProfilePerformanceEstimator>();

    vector<LoopAA *> specAAs;
    if (EnableLamp) {
      auto &smtxMan = getAnalysis<SmtxSpeculationManager>();
      auto smtxaa = new SmtxAA(&smtxMan, perf); // LAMP
      smtxaa->InitializeLoopAA(this, *DL);
      specAAs.push_back(smtxaa);
    }

    if (EnableSlamp) {
      auto slamp = &getAnalysis<SLAMPLoadProfile>();
      auto slampaa = new SlampOracleAA(slamp);
      slampaa->InitializeLoopAA(this, *DL);
      specAAs.push_back(slampaa);
    }

    if (EnableEdgeProf) {
      auto ctrlspec =
          getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
      auto edgeaa = new EdgeCountOracle(ctrlspec); // Control Spec
      edgeaa->InitializeLoopAA(this, *DL);
      specAAs.push_back(edgeaa);

      // auto killflow_aware = &getAnalysis<KillFlow_CtrlSpecAware>(); // KillFlow
      // auto callsite_aware = &getAnalysis<CallsiteDepthCombinator_CtrlSpecAware>();
      // // CallsiteDepth

      // Setup
      ctrlspec->setLoopOfInterest(loop->getHeader());
      // killflow_aware->setLoopOfInterest(ctrlspec, loop);
      // callsite_aware->setLoopOfInterest(ctrlspec, loop);
    }

    if (EnableSpecPriv) {
      auto predspec = getAnalysis<ProfileGuidedPredictionSpeculator>()
                          .getPredictionSpecPtr();
      auto predaa = new PredictionAA(predspec, perf); // Value Prediction
      predaa->InitializeLoopAA(this, *DL);

      auto &ptrresMan =
          getAnalysis<PtrResidueSpeculationManager>();
      auto ptrresaa =
          new PtrResidueAA(*DL, ptrresMan, perf); // Pointer Residue SpecPriv
      ptrresaa->InitializeLoopAA(this, *DL);

      auto spresults =
          &getAnalysis<ReadPass>().getProfileInfo(); // SpecPriv Results
      auto classify = &getAnalysis<Classify>();      // SpecPriv Classify

      // cannot validate points-to object info.
      // should only be used within localityAA validation only for points-to
      // heap use it to explore coverage. points-to is always avoided
      auto pointstoaa = new PointsToAA(*spresults);
      pointstoaa->InitializeLoopAA(this, *DL);

      specAAs.push_back(predaa);
      specAAs.push_back(ptrresaa);
      specAAs.push_back(pointstoaa);

      predaa->setLoopOfInterest(loop);

      const HeapAssignment &asgn = classify->getAssignmentFor(loop);
      if (!asgn.isValidFor(loop)) {
        errs() << "ASSIGNMENT INVALID FOR LOOP: "
               << loop->getHeader()->getParent()->getName()
               << "::" << loop->getHeader()->getName() << '\n';
      }

      const Ctx *ctx = spresults->getCtx(loop);

      auto roaa = new ReadOnlyAA(*spresults, asgn, ctx, perf);
      roaa->InitializeLoopAA(this, *DL);

      auto localaa = new ShortLivedAA(*spresults, asgn, ctx, perf);
      localaa->InitializeLoopAA(this, *DL);

      specAAs.push_back(roaa);
      specAAs.push_back(localaa);
    }

    // FIXME: try to add txio and commlib back to PDG Building
    auto txioaa = new TXIOAA();
    txioaa->InitializeLoopAA(this, *DL);
    specAAs.push_back(txioaa);

    auto commlibsaa = new CommutativeLibsAA();
    commlibsaa->InitializeLoopAA(this, *DL);
    specAAs.push_back(commlibsaa);

    auto simpleaa = new SimpleAA();
    simpleaa->InitializeLoopAA(this, *DL);
    specAAs.push_back(simpleaa);

    return specAAs;
  }

  // initialize some critics
  std::vector<Critic_ptr> getCritics(PerformanceEstimator *perf,
      unsigned threadBudget,
      LoopProfLoad *lpl) {
    std::vector<Critic_ptr> critics;

    // PS-DSWP critic
    critics.push_back(std::make_shared<PSDSWPCritic>(perf, threadBudget, lpl));

    // DSWP critic
    critics.push_back(std::make_shared<DSWPCritic>(perf, threadBudget, lpl));

    return critics;
  }

  bool Planner::parallelizeLoop(Module &M, Loop *loop) {

    BasicBlock *hA = loop->getHeader();
    Function *fA = hA->getParent();
    noelle::PDG *pdg = nullptr;
    noelle::PDG *spec_pdg = nullptr;

    auto aa = getAnalysis<LoopAA>().getTopAA();

    // Get NOELLE's PDG (conservative)
    REPORT_DUMP(aa->dump());

    auto& noelle = getAnalysis<Noelle>();
    auto loopStructures = noelle.getLoopStructures(fA);
    llvm::noelle::LoopDependenceInfo *ldi = nullptr;
    // FIXME: is there a best way to get the LDI?
    for (auto &loopStructure : *loopStructures) {
      if (loopStructure->getHeader() == hA) {
        ldi = noelle.getLoop(loopStructure);
        pdg = ldi->getLoopDG();
        // FIXME: do we need to make a copy to persist the PDG?

        // std::string pdgDotName = "pdg_" + hA->getName().str() + "_" + fA->getName().str() + ".dot";
        // writeGraph<PDG>(pdgDotName, pdg);
        break;
      }
    }

    // Set up the speculative analyses (for memory analysis)
    // Get the specutive memory dependence graph
    auto loopAAs = addAndSetupSpecModulesToLoopAA(M, loop);
    REPORT_DUMP(aa->dump());
    spec_pdg = ldi->getLoopDG();
    for (auto &loopAA : loopAAs) {
      delete loopAA;
    }

    // Set up additional remediators (controlspec, redux, memver)
    std::vector<Remediator_ptr> remediators =
      getAvailableRemediators(loop, pdg);

    // get all possible criticisms

    // FIXME: a very inefficient way to do this, essentially
    // create a new Criticism for each edge
    Criticisms allCriticisms = Critic::getAllCriticisms(*pdg);

    // modify the PDG by annotating the remedies
    for (auto &remediator : remediators) {
      Remedies remedies = remediator->satisfy(*pdg, loop, allCriticisms);
      // for each remedy
      for (Remedy_ptr r : remedies) {
        long tcost = 0;
        Remedies_ptr remedSet = std::make_shared<Remedies>();

        // expand remedy if there's subremedies
        std::function<void(Remedy_ptr)> expandSubRemedies = [remedSet, &tcost, &expandSubRemedies](Remedy_ptr r) {
          if (r->hasSubRemedies()) {
            for (Remedy_ptr subR : *r->getSubRemedies()) {
              remedSet->insert(subR);
              tcost += subR->cost;
              expandSubRemedies(subR);
            }
          } else {
            remedSet->insert(r);
            tcost += r->cost;
          }
        };

        expandSubRemedies(r);

        // for each criticism that is satisfied by the remedy
        // might be multiple ones because one remedy can solve multiple criticisms
        for (Criticism *c : r->resolvedC) {
          // remedies are added to the edges.
          c->setRemovable(true);
          c->addRemedies(remedSet);
        }
      }
    }

    // At this stage, the PDG has all the speculative information
    // We need to do the most optimistic planning

    // Get the critics
    auto lpl = &getAnalysis<LoopProfLoad>();
    auto perf = &getAnalysis<ProfilePerformanceEstimator>();
    // FIXME: make this variable
    auto threadBudget = 22;
    auto critics = getCritics(perf, threadBudget, lpl);

    // Initialize the Orchestrator
    std::unique_ptr<Orchestrator> orch = std::make_unique<Orchestrator>(*this);

    auto strategy = orch->findBestStrategy(loop, *pdg, critics);

    return false;
  }

  bool Planner::runOnModule(Module &m) {

    auto &targets = getAnalysis< Targets >();
    auto &mloops = getAnalysis< ModuleLoops >();
    for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i) {
      parallelizeLoop(m, *i);
    }

    return false;
  }

  char Planner::ID = 0;
  static RegisterPass< Planner > rp("planner", "Parallelization Planner");

} // namespace liberty
