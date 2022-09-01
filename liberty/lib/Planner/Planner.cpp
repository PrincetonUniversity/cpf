/*
 * Parallelization Planner Pass
 * ===========================
 * This pass is designed to drive the parallelization planning of CPF.
 * It queries NOELLE to get PDG, then use additional remediators
 * to remove dependences in the form of remedies.
 * It then sends the PDG with removable edges to the critics that
 * generates a parallelization plan while trying to balance the
 * parallelization gain and the remedy costs.
 * The planner then studies the compatibility of the parallelizable
 * loops and choose the set to calculate the final estimated speedup.
 *
 */
#include <memory>

#include "liberty/GraphAlgorithms/Ebk.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/Orchestrator.h"
#include "liberty/Planner/Planner.h"
#include "liberty/Utilities/WriteGraph.h"
#include "noelle/core/Noelle.hpp"
#include "scaf/MemoryAnalysisModules/KillFlow.h"
#include "scaf/MemoryAnalysisModules/NoEscapeFieldsAA.h"
#include "scaf/MemoryAnalysisModules/PureFunAA.h"
#include "scaf/MemoryAnalysisModules/SemiLocalFunAA.h"
#include "scaf/SpeculationModules/GlobalConfig.h"
#include "scaf/SpeculationModules/SLAMPLoad.h"
#include "scaf/SpeculationModules/SlampOracleAA.h"
#include "scaf/Utilities/ReportDump.h"

#define ThreadBudget 22
#define FixedPoint 1000

namespace liberty {
  using namespace llvm::noelle;
  using namespace llvm;
  using namespace liberty::slamp;


  using Vertices = std::vector<Loop *>;
  using Strategies = std::vector<Orchestrator::Strategy *>;
  using LoopToTransCalledFuncs = std::unordered_map<const Loop *, std::unordered_set<const Function *>>;

  void Planner::getAnalysisUsage(AnalysisUsage &au) const {
    au.addRequired<Noelle>(); // NOELLE is needed to drive the analysis
    au.addRequired<LoopProfLoad>();
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

    // FIXME: lets ignore SpecPriv for now
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

    if (EnableSlamp) {
      auto slamp = &getAnalysis<SLAMPLoadProfile>();
      auto slampaa = std::make_unique<SlampOracleAA>(slamp);
      remeds.push_back(std::move(slampaa));
    }

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

    // FIXME: this loopAA might be empty unless we add aa passes to required
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

    remeds.push_back(std::make_unique<TXIOAA>());
    // remeds.push_back(std::make_unique<CommutativeLibsAA>());

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

    // // FIXME: seems to be useless
    // auto simpleaa = new SimpleAA();
    // simpleaa->InitializeLoopAA(this, *DL);
    // specAAs.push_back(simpleaa);

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

  Orchestrator::Strategy *Planner::parallelizeLoop(Module &M, Loop *loop, Noelle &noelle) {
    // Get NOELLE's PDG
    // It can be conservative or optimistic based on the loopaa passed to NOELLE
    BasicBlock *hA = loop->getHeader();
    Function *fA = hA->getParent();
    errs() << "Parallelizing loop: " << fA->getName() << ":" << hA->getName() << '\n';

    auto loopStructures = noelle.getLoopStructures(fA);
    LoopStructure loopStructure(loop);
    auto ldi = noelle.getLoop(&loopStructure);

    auto pdg = ldi->getLoopDG();
    std::string pdgName = fA->getName().str() + "." + hA->getName().str() + ".dot";

    assert(pdg != nullptr && "PDG is null?");
    writeGraph<PDG>(pdgName, pdg);

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
        // TODO: is this even necessary? MemSpecAARemed uses subRemedies, but if
        // no other ones are using it, shound we remove this concept to reduce
        // the complexity
        std::function<void(Remedy_ptr)> expandSubRemedies =
            [remedSet, &tcost, &expandSubRemedies](Remedy_ptr r) {
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
    auto critics = getCritics(perf, ThreadBudget, lpl);

    // Initialize the Orchestrator
    std::unique_ptr<Orchestrator> orch = std::make_unique<Orchestrator>(*this);

    auto strategy = orch->findBestStrategy(loop, *pdg, critics);

    return strategy;
  }

  void getCalledFuns(llvm::noelle::CallGraphFunctionNode *cgNode,
      unordered_set<const Function *> &calledFuns) {
    for (auto callEdge : cgNode->getOutgoingEdges()) {
      auto *succ = callEdge->getCallee();
      auto *F = succ->getFunction();
      if (!F || calledFuns.count(F) || F->isDeclaration())
        continue;
      calledFuns.insert(F);
      getCalledFuns(succ, calledFuns);
    }
  }

  bool callsFun(const Loop *l, const Function *tgtF,
                          LoopToTransCalledFuncs &loopTransCallGraph,
                          llvm::noelle::CallGraph &callGraph) {
    if (loopTransCallGraph.count(l))
      return loopTransCallGraph[l].count(tgtF);

    for (const BasicBlock *BB : l->getBlocks()) {
      for (const Instruction &I : *BB) {
        const auto *call = dyn_cast<CallBase>(&I);
        if (!call)
          continue;
        Function *cFun = call->getCalledFunction();
        // FIXME: what about indirect calls?
        if (!cFun || cFun->isDeclaration())
          continue;
        auto *cgNode = callGraph.getFunctionNode(cFun);
        loopTransCallGraph[l].insert(cFun);
        getCalledFuns(cgNode, loopTransCallGraph[l]);
      }
    }
    return loopTransCallGraph[l].count(tgtF);
  }

  bool mustBeSimultaneouslyActive(const Loop *A, const Loop *B,
                                  LoopToTransCalledFuncs &loopTransCallGraph,
                                  llvm::noelle::CallGraph &callGraph) {

    // if A and B are in the same loop nest, they must be simultaneously active
    if (A->contains(B->getHeader()) || B->contains(A->getHeader()))
      return true;

    Function *fA = A->getHeader()->getParent();
    Function *fB = B->getHeader()->getParent();

    return callsFun(A, fB, loopTransCallGraph, callGraph) ||
      callsFun(B, fA, loopTransCallGraph, callGraph);
  }

  Edges computeEdges(const Vertices &vertices, noelle::CallGraph callGraph) {
    Edges edges;
    auto N = vertices.size();
    LoopToTransCalledFuncs loopTransCallGraph;

    for (unsigned i = 0; i < N; ++i) {
      Loop *A = vertices[i];

      BasicBlock *hA = A->getHeader();
      Function *fA = hA->getParent();

      for (unsigned j = i + 1; j < N; ++j) {
        Loop *B = vertices[j];

        BasicBlock *hB = B->getHeader();
        Function *fB = hB->getParent();

        /* If we can prove simultaneous activation,
         * exclude one of the loops */
        if (mustBeSimultaneouslyActive(A, B, loopTransCallGraph, callGraph)) {
          REPORT_DUMP(errs() << "Loop " << fA->getName() << " :: "
                             << hA->getName() << " is incompatible with loop "
                             << fB->getName() << " :: " << hB->getName()
                             << " because of simultaneous activation.\n");
          continue;
        }

        REPORT_DUMP(errs() << "Loop " << fA->getName()
                           << " :: " << hA->getName()
                           << " is COMPATIBLE with loop " << fB->getName()
                           << " :: " << hB->getName() << ".\n");
        edges.insert(Edge(i, j));
        edges.insert(Edge(j, i));
      }
    }
    return edges;
  }

  bool Planner::runOnModule(Module &m) {

    auto &targets = getAnalysis< Targets >();
    auto &mloops = getAnalysis< ModuleLoops >();

    Vertices vertices;
    VertexWeights scaledweights;
    Strategies strategies;

    auto& noelle = getAnalysis<Noelle>();

    // per hot loop
    for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
         i != e; ++i) {
      auto strategy = parallelizeLoop(m, *i, noelle);

      // if there's a strategy
      if (strategy != nullptr) {
        vertices.push_back(*i);
        scaledweights.push_back(strategy->savings);
        strategies.push_back(strategy);
      }
      // calculate the weight for the loop
    }

    // see if the loops are compatibile with each other
    auto fm = noelle.getFunctionsManager();
    auto &callGraph = *fm->getProgramCallGraph();
    auto edges = computeEdges(vertices, callGraph);

    VertexSet maxClique;
    const int wt = ebk(edges, scaledweights, maxClique);

    auto &lpl = getAnalysis<LoopProfLoad>();
    auto tt = lpl.getTotTime();
    auto speedup = tt / (tt - wt / (double)FixedPoint);

    REPORT_DUMP(errs() << "  Total expected speedup: "
                       << format("%.2f", speedup) << "x using " << ThreadBudget
                       << " workers.\n";);

    for (unsigned int v : maxClique) {
      auto *loop = vertices[v];
      auto *strat = strategies[v];

      auto header = loop->getHeader();
      auto fcn = loop->getHeader()->getParent();
      auto frac = lpl.getLoopFraction(loop);
      auto time = lpl.getLoopTime(loop);
      auto speedup = time / (time - strat->savings / (double)FixedPoint);
      REPORT_DUMP(errs() << " - " << format("%.2f", ((double)(100 * frac)))
                         << "% "
                         // << "depth " << loop->getLoopDepth() << "    "
                         << fcn->getName() << ":" << header->getName() << " "
                         << strat->pipelineStrategy 
                         << "(Loop speedup:" << format("%.2f", speedup) 
                         << " savings/loop time: " << strat->savings  << "/" << time << ")\n";);
    }

    return false;
  }

  char Planner::ID = 0;
  static RegisterPass< Planner > rp("planner", "Parallelization Planner");

} // namespace liberty
