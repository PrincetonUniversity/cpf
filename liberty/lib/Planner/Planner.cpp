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
#include "noelle/core/PDG.hpp"
#include "scaf/MemoryAnalysisModules/KillFlow.h"
#include "scaf/MemoryAnalysisModules/NoEscapeFieldsAA.h"
#include "scaf/MemoryAnalysisModules/PureFunAA.h"
#include "scaf/MemoryAnalysisModules/SemiLocalFunAA.h"
#include "scaf/SpeculationModules/GlobalConfig.h"
#include "scaf/SpeculationModules/SLAMPLoad.h"
#include "scaf/SpeculationModules/SlampOracleAA.h"
#include "scaf/Utilities/ReportDump.h"
#include "scaf/Utilities/Metadata.h"
#include "scaf/Utilities/PDGQueries.h"

#include "liberty/Utilities/json.hpp"
#include "llvm/Support/Path.h"

#define ThreadBudget 22
#define FixedPoint 1000

namespace liberty {
  using namespace llvm::noelle;
  using namespace llvm;
  using namespace liberty::slamp;

  using Vertices = std::vector<Loop *>;
  using Strategies = std::vector<Orchestrator::Strategy *>;
  using LoopToTransCalledFuncs = std::unordered_map<const Loop *, std::unordered_set<const Function *>>;

  static cl::opt<bool> ValidityCheck("validity-check", cl::init(false),
                                     cl::NotHidden,
                                     cl::desc("Check if SLAMP output is more optimistic than analysis"));
  static cl::opt<bool> SlampCheck("slamp-check", cl::init(false),
                                     cl::NotHidden,
                                     cl::desc("Check if SLAMP output matches LAMP"));

  static cl::opt<bool> AnalysisCheck("analysis-check", cl::init(false),
                                    cl::desc("Check for any deps that SLAMP removes but analysis does not"));

  static cl::opt<bool> DumpLoopStatistics("dump-loop-stats", cl::init(false),
                                          cl::desc("Dump loop statistics"));

  static cl::opt<bool> DependenceStatistics("dependence-stats", cl::init(false),
                                       cl::desc("Dump dependence statistics"));

  static cl::opt<bool> OMPStatistics("omp-stats", cl::init(false),
                                       cl::desc("Dump OMPAA statistics"));

  static cl::opt<bool> Verbose("verbose", cl::init(false), cl::desc("Print extra information for dependence statistics"));

  void Planner::getAnalysisUsage(AnalysisUsage &au) const {
    au.addRequired<Noelle>(); // NOELLE is needed to drive the analysis
    au.addRequired<LoopProfLoad>();
    au.addRequired<TargetLibraryInfoWrapperPass>();
    au.addRequired<LoopInfoWrapperPass>();
    au.addRequired<LoopAA>();
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

    if (EnableLamp) {
      auto lamp = &getAnalysis<LAMPLoadProfile>();
      auto lampaa = std::make_unique<LampOracle>(lamp);
      remeds.push_back(std::move(lampaa));
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

  // FIXME: this will be disabled
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

  nlohmann::json reportLoopParallelizationStatistics(Loop *loop, PDG *pdg) {
    BasicBlock *hA = loop->getHeader();
    Function *fA = hA->getParent();
    std::string functionName = fA->getName().str();
    std::string loopName = hA->getName().str();

    vector<DGEdge<Value> *> blockingLoopCarriedDependences;
    // get all the blocking loop-carried dependences
    for (auto &edge : pdg->getSortedDependences()) {
      if (!pdg->isInternal(edge->getIncomingT()) ||
          !pdg->isInternal(edge->getOutgoingT())) {
        continue;
      }

      if (edge->isRemovableDependence()) {
        continue;
      }

      if (!edge->isLoopCarriedDependence()) {
        continue;
      }

      // It's a blocking loop-carried dependence
      blockingLoopCarriedDependences.push_back(edge);
    }


    auto printValueToString = [](Value *value) {
      std::string valueString;
      raw_string_ostream valueStream(valueString);
      value->print(valueStream);
      return valueStream.str();
    };

    auto convertDepToJson = [&](DGEdge<Value> *edge) {
      nlohmann::json dep;
      dep["src"] = printValueToString(edge->getOutgoingT());
      dep["dst"] = printValueToString(edge->getIncomingT());

      auto *srcInst = dyn_cast<Instruction>(edge->getOutgoingT());
      if (srcInst) {
        dep["srcId"] = Namer::getInstrId(srcInst);
      } else {
        dep["srcId"] = -1;
      }

      auto *dstInst = dyn_cast<Instruction>(edge->getIncomingT());
      if (dstInst) {
        dep["dstId"] = Namer::getInstrId(dstInst);
      } else {
        dep["dstId"] = -1;
      }

      std::string depType;
      if (edge->isMemoryDependence()) {
        depType = "Memory";
        if (edge->isRAWDependence()) {
          depType += " RAW";
        } else if (edge->isWARDependence()) {
          depType += " WAR";
        } else if (edge->isWAWDependence()) {
          depType += " WAW";
        }//
         //
      } e//lse if (edge->isControlDependence()) {
        d//epType = "Control";
      } e//lse if (edge->isDataDependence()) {
        d//epType = "Data";
      }  //
      if //(edge->isLoopCarriedDependence()) {
        depType += " loop-carried";
      } else {
        depType += " loop-independent";
      }

      dep["type"] = depType;
      return dep;
    };

    nlohmann::json json;
    json["function"] = functionName;
    json["loop"] = loopName;
    json["blocking-dependences"] = nlohmann::json::array();
    for (auto &edge : blockingLoopCarriedDependences) {
      json["blocking-dependences"].push_back(convertDepToJson(edge));
    }

    return json;
  }

  Orchestrator::Strategy *Planner::parallelizeLoop(Module &M, Loop *loop, Noelle &noelle, nlohmann::json &loop_stats) {
    // Get NOELLE's PDG
    // It can be conservative or optimistic based on the loopaa passed to NOELLE
    BasicBlock *hA = loop->getHeader();
    Function *fA = hA->getParent();
    errs() << "Parallelizing loop: " << fA->getName() << ":" << hA->getName() << '\n';

    LoopStructure loopStructure(loop);
    auto ldi = noelle.getLoop(&loopStructure);

    if(DependenceStatistics) {
      LoopAA* loopAA = nullptr;
      auto aaEngines = noelle.getAliasAnalysisEngines();
      for (auto &aa: aaEngines) {
        if (aa->getName() == "SCAF") {
          loopAA = reinterpret_cast<LoopAA*>(aa->getRawPointer());
          break;
        }
      }
      if(!loopAA) { 
        errs() << "No loopAA found\n";
        exit(1);
      }
      loopAA->dump();
      //errs() << "Start DepStats for " << Namer::getBlkId(hA) << "\n";
      errs() << "Start DepStats for " << loop->getName() << "\n";
      //iterate over all instructions in loop
      nlohmann::json depstats;
      dependence_t ii = { 0, 0, 0 };
      dependence_t lc = { 0, 0, 0 };
      dependence_t ii_disproved = { 0, 0, 0 };
      dependence_t lc_disproved = { 0, 0, 0 };
      unsigned dependences_ii = 0;
      unsigned dependences_lc = 0;
      for (auto *BB1: loop->blocks()) {
        for (Instruction &I1: *BB1) {
          //if(!I1.mayReadOrWriteMemory()) continue;
          //get instruction pairs
          for (auto *BB2: loop->blocks()) {
            for (Instruction &I2: *BB2) {
              //if(!I2.mayReadOrWriteMemory()) continue;
              uint8_t intra_iter_dep = 
                disproveIntraIterationMemoryDep(&I1, &I2, 7, loop, loopAA);
              if(&I1 == &I2) intra_iter_dep = 8;
              if(intra_iter_dep != 8) {
                ii.RAW = intra_iter_dep & 0x1;
                ii.WAW = (intra_iter_dep >> 1) & 0x1;
                ii.WAR = (intra_iter_dep >> 2) & 0x1;
                ii_disproved.RAW += ii.RAW;
                ii_disproved.WAW += ii.WAW;
                ii_disproved.WAR += ii.WAR;
                dependences_ii++;
              }
              uint8_t loop_carried_dep = 
                disproveLoopCarriedMemoryDep(&I1, &I2, 7, loop, loopAA);
              if(loop_carried_dep != 8) {
                lc.RAW = loop_carried_dep & 0x1;
                lc.WAW = (loop_carried_dep >> 1) & 0x1;
                lc.WAR = (loop_carried_dep >> 2) & 0x1;
                lc_disproved.RAW += lc.RAW;
                lc_disproved.WAW += lc.WAW;
                lc_disproved.WAR += lc.WAR;
                dependences_lc++;
              }
              if(Verbose) {
                if(intra_iter_dep != 8 || loop_carried_dep != 8) {
                  errs() << I1 << " (" << Namer::getInstrId(&I1) << ")\n";
                  errs() << I2 << " (" << Namer::getInstrId(&I2) << ")\n";
                  if(intra_iter_dep != 8)
                    errs() << "ii: " << unsigned(ii.RAW) << " "
                      << unsigned(ii.WAW) << " " << unsigned(ii.WAR) << "\n";
                  if(loop_carried_dep != 8)
                    errs() << "lc: " << unsigned(lc.RAW) << " " 
                      << unsigned(lc.WAW) << " " << unsigned(lc.WAR) << "\n";
                }
              }
            }
          }

        }
      }
      errs() << "Disproved loop-carried deps for Loop " << loop->getName() << ": " << unsigned(lc_disproved.RAW) <<
                " " << unsigned(lc_disproved.WAW) << " " << unsigned(lc_disproved.WAR) <<  " out of " << dependences_lc << "\n";
      //errs() << "Disproved intra-iteration deps for Loop " << loop->getName() << ": " << unsigned(ii_disproved.RAW) <<
      //          " " << unsigned(ii_disproved.WAW) << " " << unsigned(ii_disproved.WAR) <<  " out of " << dependences_ii <<  "\n";
      errs() << "End DepStats\n";
    }

    auto pdg = ldi->getLoopDG();
    assert(pdg != nullptr && "PDG is null?");
    if(DumpLoopStatistics) {
      std::string pdgName = fA->getName().str() + "." + hA->getName().str() + ".dot";
      writeGraph<PDG>(pdgName, pdg);
    }

     //Check SLAMP against LAMP's results
    if (SlampCheck) {
      uint32_t edgeCount = 0;
      uint32_t diffCount = 0;
      auto remed_slamp = &getAnalysis<SLAMPLoadProfile>();
      auto remed_slamp_aa = std::make_unique<SlampOracleAA>(remed_slamp);
      auto remed_lamp = &getAnalysis<LAMPLoadProfile>();
      auto remed_lamp_aa = std::make_unique<LampOracle>(remed_lamp);

      for (auto &edge : make_range(pdg->begin_edges(), pdg->end_edges())) {
        if (!pdg->isInternal(edge->getIncomingT()) ||
            !pdg->isInternal(edge->getOutgoingT()))
          continue;

        auto *src = dyn_cast<Instruction>(edge->getOutgoingT());
        auto *dst = dyn_cast<Instruction>(edge->getIncomingT());
        assert(src && dst && "src/dst not instructions in PDG?");
        bool loopCarried = false;
        edgeCount++;

        // LAMP is unable to reason about function calls,
        // and SLAMP only considers RAW deps.
        if (dyn_cast<CallBase>(src) || dyn_cast<CallBase>(dst))
          continue;
        if (!edge->isRAWDependence())
          continue;

        if (edge->isLoopCarriedDependence())
          loopCarried = true;

        auto slamp_remedy = remed_slamp_aa->memdep(src, dst, loopCarried,
                                                   DataDepType::RAW, loop);
        auto lamp_remedy = remed_lamp_aa->memdep(src, dst, loopCarried,
                                                 DataDepType::RAW, loop);

        if (slamp_remedy.depRes != lamp_remedy.depRes) {
          diffCount++;
          errs() << "SLAMP: " << slamp_remedy.depRes
                 << ", LAMP: " << lamp_remedy.depRes << "\n";
        }
      }
      errs() << "Total number of edges: " << edgeCount
             << ", number of diff edges: " << diffCount << "\n";
    }

    // Make sure SLAMP is not more conservative than analysis
    // Since SLAMP only checks for RAW memory deps,
    // make sure such an edge does not exist according to anlysis.
    // If it exists according to SLAMP, there is a bug
    if (ValidityCheck) {
      auto remed_slamp = &getAnalysis<SLAMPLoadProfile>();
      auto remed_slamp_aa = std::make_unique<SlampOracleAA>(remed_slamp);

      for(auto nodeI : pdg->getNodes()) {
        for(auto nodeJ : pdg->getNodes()) {
           auto* src = dyn_cast<Instruction>(nodeI->getT());
           auto* dst = dyn_cast<Instruction>(nodeJ->getT());
           if(!src || !dst) continue;
           if(!src->mayReadOrWriteMemory() || !dst->mayReadOrWriteMemory()) continue;
           if(!src->mayWriteToMemory() && !dst->mayWriteToMemory()) continue;
             

           auto deps = pdg->getDependences(src, dst);
           bool relevantIntraDepExists = false;
           bool relevantInterDepExists = false;
           // Check intra and iter iteration deps separately
           for(auto dep : deps) {
             if(dep->isMemoryDependence() && dep->isRAWDependence()) {
               if(dep->isLoopCarriedDependence())
                 relevantInterDepExists = true;
               else
                 relevantIntraDepExists = true;
             }
           }
           if(!relevantInterDepExists) {
             auto slamp_dep_inter = remed_slamp_aa->memdep(src, dst, true, DataDepType::RAW, loop);
             if(slamp_dep_inter.depRes == Dep) {
              errs() << "ERROR: SLAMP is more conservative than analysis for loop carried!\n";
              errs() << *src << " (" << Namer::getInstrId(src) << ")\n";
              errs() << *dst << " (" << Namer::getInstrId(dst) << ")\n";
             }
           }
           if(!relevantIntraDepExists) {
             auto slamp_dep_intra = remed_slamp_aa->memdep(src, dst, false, DataDepType::RAW, loop);
             if(slamp_dep_intra.depRes == Dep) {
             errs() << "ERROR: SLAMP is more conservative than analysis for intra iter!\n";
             errs() << *src << " (" << Namer::getInstrId(src) << ")\n";
             errs() << *dst << " (" << Namer::getInstrId(dst) << ")\n";
             }
           }
        }
      }
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
        unsigned long tcost = 0;
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
          if(AnalysisCheck && remediator->getRemediatorName() == "slamp-oracle-remed")
            errs() << "Removed dep from " << *(c->getOutgoingT()) << " to " << *(c->getIncomingT()) << "\n";
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

    if (DumpLoopStatistics) {
      // print it out to a file
      loop_stats = reportLoopParallelizationStatistics(loop, pdg);
    }
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
    auto &lpl = getAnalysis<LoopProfLoad>();
    liberty::slamp::SLAMPLoadProfile *slamp = nullptr;
    if (EnableSlamp) {
      slamp = &getAnalysis<SLAMPLoadProfile>();
    }

    nlohmann::json loop_stats;
    unsigned loop_idx = 0;
    // per hot loop
    for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
         i != e; ++i) {
      auto strategy = parallelizeLoop(m, *i, noelle, loop_stats[loop_idx]);

      if (DumpLoopStatistics) {
        // get loop time
        auto loop = *i;
        auto coverage = lpl.getLoopFraction(loop);
        auto time = lpl.getLoopTime(loop);
        if (strategy != nullptr) {
          auto speedup = time / (time - strategy->savings / (double)FixedPoint);
          std::string strategyString;
          raw_string_ostream fout(strategyString);
          fout << strategy->pipelineStrategy;
          loop_stats[loop_idx]["speedup"] = speedup;
          loop_stats[loop_idx]["pipeline"] = fout.str();
        }
        loop_stats[loop_idx]["coverage"] = coverage;
        loop_stats[loop_idx]["time"] = time;
      }

      // if there's a strategy
      if (strategy != nullptr) {
        vertices.push_back(*i);
        scaledweights.push_back(strategy->savings);
        strategies.push_back(strategy);
      }
      loop_idx++;
    }

    if (vertices.size() == 0) {
      errs() << "\n\n"
             << "*********************************************************************\n"
             << "No parallelizing transformation applicable to /any/ of the "
                "hot loops.\n"
             << "*********************************************************************\n";
    } else {
      errs() << "\n\n*********************************************************************\n"
             << "Parallelizable loops:\n";
    }
    for (int v = 0; v < vertices.size(); ++v) {
      auto *loop = vertices[v];
      auto *strat = strategies[v];

      auto header = loop->getHeader();
      auto fcn = loop->getHeader()->getParent();
      auto frac = lpl.getLoopFraction(loop);
      auto time = lpl.getLoopTime(loop);
      auto speedup = time / (time - strat->savings / (double)FixedPoint);

      string slampStr = " (NO SLAMP)";
      if (EnableSlamp) {
        if (slamp->isTargetLoop(loop))
          slampStr = " (SLAMP)";
      }
      REPORT_DUMP(errs() << " - " << format("%.2f", ((double)(100 * frac)))
                         << "% "
                         // << "depth " << loop->getLoopDepth() << "    "
                         << fcn->getName() << " :: " << header->getName() << " "
                         << strat->pipelineStrategy
                         << " (Loop speedup: " << format("%.2f", speedup)
                         << "x savings/loop time: " << strat->savings/FixedPoint  << "/" << time << ")" << slampStr <<"\n";);
    }

    // see if the loops are compatibile with each other
    auto fm = noelle.getFunctionsManager();
    auto &callGraph = *fm->getProgramCallGraph();
    auto edges = computeEdges(vertices, callGraph);

    VertexSet maxClique;
    const int wt = ebk(edges, scaledweights, maxClique);

    auto totalTime = lpl.getTotTime();
    auto speedup = totalTime / (totalTime - wt / (double)FixedPoint);

    // do a check if the max clique adds up to more than total time

    unsigned long totalTimeClique = 0;
    for (unsigned int v : maxClique) {
      totalTimeClique += lpl.getLoopTime(vertices[v]);
    }

    if (totalTimeClique > totalTime) {
      errs() << "WARNING: The max clique is larger than the total time. This "
                "should not happen.\n";
    }

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
                         << fcn->getName() << " :: " << header->getName() << " "
                         << strat->pipelineStrategy
                         << " (Loop speedup: " << format("%.2f", speedup)
                         << "x savings/loop time: " << strat->savings / FixedPoint << "/" << time << ")\n";);
    }

    if (DumpLoopStatistics) {
      // dump the loop stats to a file
      std::string filename = "loop_stats.json";
      std::ofstream file(filename);
      file << loop_stats.dump(2);
      file.close();
    }

    return false;
  }

  char Planner::ID = 0;
  static RegisterPass< Planner > rp("planner", "Parallelization Planner");

} // namespace liberty
