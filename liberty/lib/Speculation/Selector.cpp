#define DEBUG_TYPE "selector"

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/DomPrinter.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Speculation/Selector.h"
#include "liberty/GraphAlgorithms/Ebk.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/Orchestrator.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/UpdateOnCloneAdaptors.h"
#include "liberty/Strategy/PipelineStrategy.h"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "liberty/Utilities/WriteGraph.h"

#include "scaf/MemoryAnalysisModules/KillFlow.h"
#include "scaf/SpeculationModules/GlobalConfig.h"
#include "scaf/SpeculationModules/LocalityAA.h"
#include "scaf/SpeculationModules/PDGBuilder.hpp"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/Utilities/PrintDebugInfo.h"
#include "scaf/Utilities/ReportDump.h"

#include "noelle/core/DGGraphTraits.hpp"
#include "noelle/core/DominatorSummary.hpp"
#include "noelle/core/FunctionsManager.hpp"
#include "noelle/core/LoopDependenceInfo.hpp"
#include "noelle/core/Noelle.hpp"
#include "noelle/core/PDG.hpp"
#include "noelle/core/PDGAnalysis.hpp"

#include <algorithm>
#include <iomanip>

using namespace llvm;
using namespace llvm::noelle;

namespace liberty::SpecPriv
{

static cl::opt<unsigned> LateInlineMinCallsiteCoverage(
  "late-inline-min-coverage", cl::init(10), cl::NotHidden,
  cl::desc("Only consider late inlining for callsites whose coverage is at least this percent (default 10)"));
static cl::opt<unsigned> LateInlineMaxRounds(
  "late-inline-max-rounds", cl::init(1), cl::NotHidden,
  cl::desc("Perform no more than N rounds of late inlining; set to 1 for no late inlining (default 10)"));
static cl::opt<bool> IgnoreExpectedSpeedup(
  "ignore-expected-speedup", cl::init(false), cl::Hidden,
  cl::desc("Select for transformation, even if speedup is expected to suck."));
static cl::opt<bool> ParallelizeAtMostOneLoop(
  "parallelize-one", cl::init(false), cl::Hidden,
  cl::desc("Select at most one loop for parallelization."));

STATISTIC(numLateInlineFunctions, "Functions inlined after during selection");
STATISTIC(numLateInlineRounds,    "Rounds of late inlining during selection");
STATISTIC(numSelected, "Parallel regions selected #regression");

static HeapAssignment null_assignment = HeapAssignment();

const HeapAssignment &Selector::getAssignment() const
{
  return null_assignment;
}

HeapAssignment &Selector::getAssignment()
{
  return null_assignment;
}

void Selector::analysisUsage(AnalysisUsage &au)
{
  // au.addRequired< Noelle >();
  // au.addRequired< PDGAnalysis >();
  au.addRequired< TargetLibraryInfoWrapperPass >();
  //au.addRequired< BlockFrequencyInfoWrapperPass >();
  //au.addRequired< BranchProbabilityInfoWrapperPass >();
  au.addRequired< PDGBuilder >();
  au.addRequired< ModuleLoops >();
  //au.addRequired< KillFlow >();
  au.addRequired< Targets >();

  // au.addRequired< LAMPLoadProfile >();

  au.addRequired< LoopProfLoad >();
  au.addRequired< ProfilePerformanceEstimator >();

  au.setPreservesAll();
}

ControlSpeculation *Selector::getControlSpeculation() const
{
  static NoControlSpeculation none;
  return &none;
}

PredictionSpeculation *Selector::getPredictionSpeculation() const
{
  static NoPredictionSpeculation none;
  return &none;
}

void Selector::computeVertices(Vertices &vertices)
{
  Pass &proxy = getPass();
  ModuleLoops &mloops = proxy.getAnalysis< ModuleLoops >();
  const Targets &targets = proxy.getAnalysis< Targets >();
  for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i)
    vertices.push_back(*i);
}

const unsigned Selector::NumThreads(22);
const unsigned Selector::FixedPoint(1000);
const unsigned Selector::PenalizeLoopNest( Selector::FixedPoint*10 );

static double getWeight(SCC *scc, PerformanceEstimator *perf) {
  double sumWeight = 0.0;

  for (auto instPair : scc->internalNodePairs()) {
    Instruction *inst = dyn_cast<Instruction>(instPair.first);
    assert(inst);

    sumWeight += perf->estimate_weight(inst);
  }

  return sumWeight;
}

static bool isParallel(const SCC &scc) {
  for (auto edge : make_range(scc.begin_edges(), scc.end_edges())) {
    if (!scc.isInternal(edge->getIncomingT()) ||
        !scc.isInternal(edge->getOutgoingT()))
      continue;

    if (edge->isLoopCarriedDependence()) {
      return false;
    }
  }
  return true;
}

/*
 * The structure that stores the coverage information
 */
struct CoverageStats {
public:
  double TotalWeight;
  double LargestSeqWeight;
  double ParallelWeight;
  double SequentialWeight;

  std::string dumpPercentage() {
    if (TotalWeight == 0) {
      return "Coverage incomplete: loop weight is 0\n";
    }

    double ParallelPercentage = 100 * ParallelWeight / TotalWeight;
    double SequentialPercentage = 100 * SequentialWeight / TotalWeight;
    double CriticalPathPercentage = 100 * LargestSeqWeight / TotalWeight;
    double ParallelismCoverage = 100 - CriticalPathPercentage;

    std::stringstream ss;
    ss << "Largest Seq SCC (%): " << std::fixed << std::setw(2)
       << CriticalPathPercentage << "\n"
       << "Parallel SCC (%): " << ParallelPercentage << "\n"
       << "Sequential SCC (%): " << SequentialPercentage << "\n"
       << "Paralleism (%): " << ParallelismCoverage << "\n";

    return ss.str();
  }

  CoverageStats(LoopDependenceInfo *ldi, PDG &pdg, PerformanceEstimator *perf, LAMPLoadProfile *lamp) {
    std::vector<Value *> loopInternals;
    for (auto internalNode : pdg.internalNodePairs()) {
      loopInternals.push_back(internalNode.first);
    }

    std::unordered_set<DGEdge<Value> *> edgesToIgnore;

    // go through the PDG and add all removable edges to this set
    for (auto edge : pdg.getEdges()) {
      if (edge->isRemovableDependence()) {
        edgesToIgnore.insert(edge);
      }
    }

    auto optimisticPDG =
        pdg.createSubgraphFromValues(loopInternals, false, edgesToIgnore);

/*
 *    errs() << "Dumping probability\n";
 *
 *    auto distributionVec = vector<double>();
 *    for (auto edge : optimisticPDG->getEdges()) {
 *      if (edge->isLoopCarriedDependence() && !edge->isRemovableDependence()) {
 *        if (edge->isMemoryDependence()) {
 *          // FIXME: double check the source and destination
 *          auto src = dyn_cast<Instruction>(edge->getOutgoingT());
 *          auto dst = dyn_cast<Instruction>(edge->getIncomingT());
 *          if (!src || !dst) {
 *            continue;
 *          }
 *          // if either one is function call
 *          // LAMP does not handle function call
 *          if (isa<CallBase>(src) || isa<CallBase>(dst)) {
 *            continue;
 *          }
 *
 *          // the source and dst are reversed
 *          double prob = lamp->probDep(loop->getHeader(), dst, src, 1);
 *          //double prob = lamp->probDep(0, src, dst, 1);
 *
 *          double probSrc = perf->estimate_parallelization_weight(src, loop);
 *          double probDst = perf->estimate_parallelization_weight(dst, loop);
 *
 *          std::ostringstream streamOut;
 *          streamOut << std::fixed << std::setprecision(2) << "("
 *            << prob << ", " << probSrc << ", " << probDst << ")";
 *
 *          distributionVec.push_back(prob);
 *          // REPORT_DUMP(errs() << "(" << prob * 100 << " %) " << *src;
 *          REPORT_DUMP(errs() << streamOut.str() << *src;
 *                      liberty::printInstDebugInfo(src); errs() << " to " << *dst;
 *                      liberty::printInstDebugInfo(dst); errs() << "\n");
 *        }
 *      }
 *    }
 *
 *    REPORT_DUMP(
 *        errs() << "prob_dist: [";
 *        for (auto prob : distributionVec) {
 *          errs() << std::to_string(prob) <<  ",";
 *        }
 *        errs() << "]\n";
 *    );
 *
 */

    // get sccdag from noelle
    auto sccManager = ldi->getSCCManager();
    auto sccdag = sccManager->getSCCDAG();

    // auto optimisticSCCDAG = new SCCDAG(optimisticPDG);

    TotalWeight = 0;
    LargestSeqWeight = 0;
    ParallelWeight = 0;
    SequentialWeight = 0;

    // get total weight
    for (auto *scc : sccdag->getSCCs()) {
      auto curWeight = getWeight(scc, perf);
      TotalWeight += curWeight;
      if (isParallel(*scc)) {
        ParallelWeight += curWeight;
      } else {
        SequentialWeight += curWeight;
        LargestSeqWeight = std::max(LargestSeqWeight, curWeight);
      }
    }
  }
};

/*
 * Go through each loop (vertice) and compute the best parallelized weights
 */
unsigned Selector::computeWeights(const Vertices &vertices, Edges &edges,
                         VertexWeights &weights, VertexWeights &scaledweights,
                         LateInliningOpportunities &opportunities) {
  unsigned numApplicable = 0;

  // have to have a concrete LLVM pass
  Pass &proxy = getPass();

  ModuleLoops &mloops = proxy.getAnalysis< ModuleLoops >();

  // LoopProf should always be alive
  LoopProfLoad &lpl = proxy.getAnalysis< LoopProfLoad >();

  PDGBuilder &pdgBuilder = proxy.getAnalysis< PDGBuilder >();

  // create Orchestrator
  std::unique_ptr<Orchestrator> orch = std::unique_ptr<Orchestrator>(new Orchestrator(proxy));

  if (EnableEdgeProf) {
    ControlSpeculation *ctrlspec =
      proxy.getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
  }

  if (EnableSpecPriv) {
    const Read &rd = proxy.getAnalysis<ReadPass>().getProfileInfo();
    Classify &classify = proxy.getAnalysis<Classify>();
  }


/*  // Memory dependences speculation now moved to SCAF
 *  PredictionSpeculation *loadedValuePred =
 *      &proxy.getAnalysis<ProfileGuidedPredictionSpeculator>();
 *  SmtxSpeculationManager &smtxLampMan =
 *      proxy.getAnalysis<SmtxSpeculationManager>();
 *  PtrResidueSpeculationManager &ptrResMan =
 *      proxy.getAnalysis<PtrResidueSpeculationManager>();
 *  LAMPLoadProfile &lamp = proxy.getAnalysis<LAMPLoadProfile>();
 *  KillFlow &kill = proxy.getAnalysis< KillFlow >(); */
  // LAMPLoadProfile &lamp = proxy.getAnalysis<LAMPLoadProfile>();

  const unsigned N = vertices.size();
  weights.resize(N);
  scaledweights.resize(N);

  // go through each loop
  for(unsigned i=0; i<N; ++i)
  {
    Loop *A = vertices[i];
    BasicBlock *hA = A->getHeader();
    Function *fA = hA->getParent();

    REPORT_DUMP(errs()
          << "\n\n=--------------------------------------------------------"
             "----------------------=\nCompute weight for loop "
          << fA->getName() << " :: " << hA->getName() << "...\n");

    // estimate the weight of a loop
    double loopTime = perf->estimate_loop_weight(A);
    const unsigned long scaledLoopTime = FixedPoint*loopTime;

    // adjust the loop weight by deducting the depth penalty
    // FIXME: ugly code
    assert(A->getLoopDepth() > 0 && "Target loop is not a loop???");
    const unsigned long depthPenalty = PenalizeLoopNest*(A->getLoopDepth()-1); // break ties with nested loops
    unsigned long adjLoopTime = scaledLoopTime;
    if( scaledLoopTime > depthPenalty )
      adjLoopTime = scaledLoopTime - depthPenalty;
    else if (scaledLoopTime > depthPenalty / 10)
      adjLoopTime = scaledLoopTime - depthPenalty / 10;

    {
      // Dump PDG
      //std::unique_ptr<llvm::noelle::PDG> pdg = pdgBuilder.getLoopPDG(A);
      // llvm::noelle::PDG *pdg =  nullptr;// pdgBuilder.getLoopPDG(A).release();

      // old way of getting PDG
      llvm::noelle::PDG *pdg = pdgBuilder.getLoopPDG(A).release();
      std::string pdgDotName = "pdg_" + hA->getName().str() + "_" + fA->getName().str() + ".dot";
      writeGraph<PDG>(pdgDotName, pdg);

      // // get PDG from NOELLE
      // auto& noelle = proxy.getAnalysis<Noelle>();
      // auto loopStructures = noelle.getLoopStructures(fA);

      // llvm::noelle::LoopDependenceInfo *ldi = nullptr;
      // // FIXME: is there a best way to get the LDI?
      // for (auto &loopStructure : *loopStructures) {
      //   if (loopStructure->getHeader() == hA) {
      //     ldi = noelle.getLoop(loopStructure);
      //     pdg = ldi->getLoopDG();

      //     std::string pdgDotName = "pdg_" + hA->getName().str() + "_" + fA->getName().str() + ".dot";
      //     writeGraph<PDG>(pdgDotName, pdg);
      //     break;
      //   }
      // }

      if (pdg == nullptr) {
        errs() << "No PDG found for loop " << fA->getName() << " :: " << hA->getName() << "\n";
        continue;
      }

      // trying to find the best parallelization strategy for this loop
      REPORT_DUMP(
          errs() << "Run Orchestrator:: find best parallelization strategy for "
                 << fA->getName() << " :: " << hA->getName() << "...\n");


      std::unique_ptr<PipelineStrategy> ps;
      std::unique_ptr<SelectedRemedies> sr;
      Critic_ptr sc;

      // try multiple critics, find the best one for the loop by estimation with number of threads
      bool applicable = orch->findBestStrategy(A, *pdg, ps, sr, sc, NumThreads);
          // pipelineOption_ignoreAntiOutput(),
          // pipelineOption_includeReplicableStages(),
          // pipelineOption_constrainSubLoops(),
          // pipelineOption_abortIfNoParallelStage());

      // the pdg is updated over here
      // CoverageStats stats(A, *pdg, perf, &lamp);
      // CoverageStats stats(ldi, *pdg, perf, nullptr);

      // REPORT_DUMP(
          // errs() << stats.dumpPercentage());

      if( applicable )
      {
        ++numApplicable;
        ps->setValidFor( hA );

        unsigned long  estimatePipelineWeight = (unsigned long) FixedPoint*perf->estimate_pipeline_weight(*ps, A);
        const long wt = adjLoopTime - estimatePipelineWeight;
        unsigned long scaledwt = 0;

        /*
         *errs() << "wt: " << wt << "\nadjLoopTime: " << adjLoopTime
         *       << "\nestimatePipelineWeight: " << estimatePipelineWeight
         *       << "\ndepthPenalty: " << depthPenalty << '\n';
         */

        ps->dump_pipeline(errs());

        if (perf->estimate_loop_weight(A))
          scaledwt = wt * (double)lpl.getLoopTime(hA) / (double)perf->estimate_loop_weight(A);

        if( wt < 0 )
        {
          weights[i] = 0;
          scaledweights[i] = 0;
        }
        else
        {
          weights[i] = wt;
          scaledweights[i] = scaledwt;
        }

        REPORT_DUMP(errs() << "Parallelizable Loop " << fA->getName()
                     << " :: " << hA->getName() << " has expected savings "
                     << weights[i] << '\n');

        findLateInliningOpportunities(A,*ps,*perf,opportunities);

        strategies[ hA ] = std::move(ps);
        selectedRemedies[ hA ] = std::move(sr);
        selectedCritics[ hA ] = sc;
        //loopDepInfo [hA] = std::move(ldi);
        selectedLoops.insert(hA);

      } else {
        REPORT_DUMP(errs() << "No parallelizing transform applicable to "
                     << fA->getName() << " :: " << hA->getName() << '\n';);

        weights[i] = 0;
        scaledweights[i] = 0;
        for (unsigned v = 0; v < N; ++v) {
          edges.erase(Edge(i, v));
          edges.erase(Edge(v, i));
        }
      }
      delete pdg;
    }
  }

  return numApplicable;
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

bool Selector::callsFun(const Loop *l, const Function *tgtF,
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

bool Selector::mustBeSimultaneouslyActive(
    const Loop *A, const Loop *B, LoopToTransCalledFuncs &loopTransCallGraph,
    llvm::noelle::CallGraph &callGraph) {

  // if A and B are in the same loop nest, they must be simultaneously active
  if (A->contains(B->getHeader()) || B->contains(A->getHeader()))
    return true;

  Function *fA = A->getHeader()->getParent();
  Function *fB = B->getHeader()->getParent();

  return callsFun(A, fB, loopTransCallGraph, callGraph) ||
         callsFun(B, fA, loopTransCallGraph, callGraph);
}

void Selector::computeEdges(const Vertices &vertices, Edges &edges)
{
  const unsigned N = vertices.size();
  LoopToTransCalledFuncs loopTransCallGraph;
  Pass &proxy = getPass();
  // auto &callGraph = proxy.getAnalysis<CallGraphWrapperPass>().getCallGraph();

  auto& pdgAnalysis = proxy.getAnalysis<PDGAnalysis>();
  // auto& noelle = proxy.getAnalysis<Noelle>();

  // get a module
  if (vertices.size() == 0)
    return;
  Loop *A = vertices[0];
  BasicBlock *hA = A->getHeader();
  Function *fA = hA->getParent();
  Module *m = fA->getParent();
  auto fm = FunctionsManager(*m, pdgAnalysis, nullptr);
  /*
   *   Call graph.
   */
  auto &callGraph = *fm.getProgramCallGraph();

  for(unsigned i=0; i<N; ++i)
  {
    Loop *A = vertices[i];

    BasicBlock *hA = A->getHeader();
    Function *fA = hA->getParent();

    for(unsigned j=i+1; j<N; ++j)
    {
      Loop *B = vertices[j];

      BasicBlock *hB = B->getHeader();
      Function *fB = hB->getParent();

      /* If we can prove simultaneous activation,
       * exclude one of the loops */
      if( mustBeSimultaneouslyActive(A, B, loopTransCallGraph,callGraph) )
      {
        REPORT_DUMP(errs() << "Loop " << fA->getName() << " :: " << hA->getName()
                     << " is incompatible with loop " << fB->getName()
                     << " :: " << hB->getName()
                     << " because of simultaneous activation.\n");
        continue;
      }

      /*
       * This requires SpecPriv
       */
      if (!compatibleParallelizations(A, B)) {
        REPORT_DUMP(errs() << "Loop " << fA->getName() << " :: " << hA->getName()
                     << " is incompatible with loop " << fB->getName()
                     << " :: " << hB->getName()
                     << " because of incompatible assignments.\n");
        continue;
      }

      REPORT_DUMP(errs() << "Loop " << fA->getName() << " :: " << hA->getName()
                   << " is COMPATIBLE with loop " << fB->getName()
                   << " :: " << hB->getName() << ".\n");
      edges.insert(Edge(i, j));
      edges.insert( Edge(j,i) );
    }
  }
}

void printOneLoopStrategy(raw_ostream &fout, Loop *loop,
                          LoopParallelizationStrategy *strategy,
                          LoopProfLoad &lpl, bool willTransform,
                          PerformanceEstimator &perf) {
  const unsigned FixedPoint(1000);
  const unsigned long tt = FixedPoint * lpl.getTotTime();
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  const unsigned long w = FixedPoint * lpl.getLoopTime(loop);

  if( willTransform )
    fout << " - ";
  else
    fout << " X ";

  // Loop coverage
  //fout << format("%.2f", ((double)(100*w)/std::max(1u,tt))) << "% ";
  fout << format("%.2f", ((double)(100*w)/tt)) << "% ";
  // Loop depth
  fout << "depth " << loop->getLoopDepth();

  fout << "    ";

  // Loop name
  fout << fcn->getName() << " :: " << header->getName();

  fout << "    ";

  if( strategy ) {
    strategy->summary(fout);
    strategy->pStageWeightPrint(fout, perf, loop);
  }
  else
    fout << "(no strat)";

  if( willTransform )
    fout << "\t\t\t#regrn-par-loop\n";
  else
    fout << "\t\t\t#regrn-no-par-loop\n";
}

const Instruction *getGravityInstFromRemed(Remedy_ptr &remed) {
  // only handle remedies that have a cost
  if (remed->getRemedyName().equals("invariant-value-pred-remedy")) {
    LoadedValuePredRemedy *loadedValuePredRemedy =
        (LoadedValuePredRemedy *)&*remed;
      return loadedValuePredRemedy->loadI;
  } else if (remed->getRemedyName().equals("locality-remedy")) {
    LocalityRemedy *localityRemed = (LocalityRemedy *)&*remed;
    if (localityRemed->type == LocalityRemedy::UOCheck) {
      if (const Instruction *gravity =
              dyn_cast<Instruction>(localityRemed->ptr))
        return gravity;
    } else if (localityRemed->type == LocalityRemedy::Private) {
      return localityRemed->privateI;
    }
  } else if (remed->getRemedyName().equals("smtx-lamp-remedy")) {
    SmtxLampRemedy *smtxLampRemedy = (SmtxLampRemedy *)&*remed;
    return smtxLampRemedy->memI;
  } else if (remed->getRemedyName().equals("ptr-residue-remedy")) {
    PtrResidueRemedy *ptrResidueRemedy = (PtrResidueRemedy *)&*remed;
    if (const Instruction *gravity =
            dyn_cast<Instruction>(ptrResidueRemedy->ptr))
      return gravity;
  }
  return nullptr;
}

void populateRemedCostPerStage(LoopParallelizationStrategy *strategy, Loop *L,
                               SelectedRemedies &remeds) {
  unsigned unknownStageCnt = 0;
  for (auto remed : remeds) {
    // find gravity inst
    const Instruction *gravity = getGravityInstFromRemed(remed);
    if (!gravity)
      continue;

    std::vector<unsigned> stages;

    if (L->contains(gravity))
      strategy->getExecutingStages(const_cast<Instruction *>(gravity), stages);
    else
    {
      // instruction not contained in the loop. Conservatively assume taht it is
      // contained in all the stages
      errs() << "Unknown stage for " << *gravity << "\n";
      unknownStageCnt++;
      for (auto j = 0; j < strategy->getStageNum(); ++j)
        stages.push_back(j);
    }

    // assign cost to the appropriate stage
    for (unsigned i : stages) {
      strategy->addRemedCostToStage(remed->cost, i);
    }
  }

  errs() << "Count of remeds with unknown gravity stage: " << unknownStageCnt
         << "\n";
}

void Selector::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
  // No-op
}

Selector::~Selector()
{
/*
  for(Loop2Strategy::iterator i=strategies.begin(), e=strategies.end(); i!=e; ++i)
    delete i->second;
*/
}

const LoopParallelizationStrategy &Selector::getStrategy(Loop *loop) const
{
  Loop2Strategy::const_iterator i = strategies.find( loop->getHeader() );
  assert( i != strategies.end() && "No strategy for that loop");
  return * i->second;
}

LoopParallelizationStrategy &Selector::getStrategy(Loop *loop)
{
  assert(strategies[loop->getHeader()] && "No strategy for that loop");
  return *strategies[loop->getHeader()];
}

void Selector::findLateInliningOpportunities(
  Loop *loop,
  const PipelineStage::ISet &iset,
  const unsigned stage_weight,
  PerformanceEstimator &perf,
  LateInliningOpportunities &opps)
{
  // Scan the instructions in this stage, looking for:
  //  - call sites
  //    - which target internally-defined functions,
  //    - which source/sink a loop-carried memory dependence, and
  //    - whose estimated running time is a decent fraction of the stage's running time.
  for(PipelineStage::ISet::const_iterator i=iset.begin(), e=iset.end(); i!=e; ++i)
  {
    // Call site
    Instruction *inst = *i;
    CallSite cs = getCallSite(inst);
    if( !cs.getInstruction() )
      continue;
    if( opps.count(inst) )
      continue;

    // Which targets an internally defined function
    const Function *callee = cs.getCalledFunction();
    if( !callee )
      continue;
    if( callee->isDeclaration() )
      continue;

    // Whose estimated runtime is a decent fraction of the stage's running time.
    const unsigned callsite_weight = perf.estimate_weight(inst);
    if( callsite_weight * 100 < stage_weight * LateInlineMinCallsiteCoverage )
      continue;

    // Which sources/sinks a loop-carried memory dep
    // TODO

    opps.insert( inst );

    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    errs() << "In loop " << fcn->getName() << " :: " << header->getName() << '\n'
           << "- the callsite " << *inst << '\n'
           << " (in block " << inst->getParent()->getName() << ")\n"
           << "- is located in a sequential stage\n"
           << "- and has weight " << callsite_weight << " / " << stage_weight << '\n'
           << "==> Recommended for late inlining.\n";
  }
}

void Selector::findLateInliningOpportunities(
  Loop *loop,
  const PipelineStage &sequential,
  PerformanceEstimator &perf,
  LateInliningOpportunities &opps)
{
  const unsigned long stage_weight =
    perf.estimate_weight( sequential.replicated.begin(), sequential.replicated.end()) +
    perf.estimate_weight( sequential.instructions.begin(), sequential.instructions.end());

  findLateInliningOpportunities(loop, sequential.replicated, stage_weight, perf, opps);
  findLateInliningOpportunities(loop, sequential.instructions, stage_weight, perf, opps);
}

void Selector::findLateInliningOpportunities(
  Loop *loop,
  const PipelineStrategy &strat,
  PerformanceEstimator &perf,
  LateInliningOpportunities &opps)
{
  // Can we find opportunities to inline calls within a sequential or replicable stage?
  for(PipelineStrategy::Stages::const_iterator i=strat.stages.begin(), e=strat.stages.end(); i!=e; ++i)
  {
    const PipelineStage &stage = *i;
    if( stage.type != PipelineStage::Parallel )
      findLateInliningOpportunities(loop, stage, perf, opps);
  }
}

void Selector::summarizeParallelizableLoops(const Vertices &vertices, const VertexWeights &weights, unsigned numApplicable)
{
  Pass &proxy = getPass();
  LoopProfLoad &lpl = proxy.getAnalysis< LoopProfLoad >();
  if( numApplicable < 1 )
  {
    errs() << "\n\n"
           << "*********************************************************************\n"
           << "No parallelizing transformation applicable to /any/ of the hot loops.\n"
           << "*********************************************************************\n";
  }
  else
  {
    errs() << "\n\n*********************************************************************\n"
           << "Parallelizable loops:\n";
    const unsigned long tt = FixedPoint * lpl.getTotTime();
    for(unsigned i=0, N=vertices.size(); i<N; ++i)
    {
      Loop *loop = vertices[i];
      BasicBlock *header = loop->getHeader();

      Loop2Strategy::const_iterator j = strategies.find(header);
      if( j == strategies.end() )
        continue;

      Function *fcn = header->getParent();

      const unsigned long w = FixedPoint * lpl.getLoopTime(header);
      errs() << "  - " << format("%.2f", ((double)(100 * w) / tt))
             << "% " << fcn->getName() << " :: " << header->getName() << ' ';
      //     errs() << "  - " << (100*w/std::max(FixedPoint,tt)) << "% " <<
      //     fcn->getName() << " :: " << header->getName() << ' ';
      j->second->summary(errs());

      const double wt = weights[i];
      const double ltt = FixedPoint * (double)lpl.getLoopTime(header);
      const double speedup = ltt / (ltt - wt);
      if( speedup < 0.0 )
      {
        errs() << '\n'
               << "Negative speedup\n"
               << " ltt: " << format("%.2f", ltt) << '\n'
               << " wt: " << format("%.2f", wt) << '\n';
        assert(false && "Negative speedup?");
      }
      errs() << " (Loop speedup: " << format("%.2f", speedup) << "x)\n";
    }
  }
}

bool Selector::doInlining(LateInliningOpportunities &opportunities)
{
  errs() << "{{" << "{\n";

  // For each opportunity:
  // - Perform inlining.
  // - reset analysys and profiles
  //  (this implementation will do edge-count, loop-time,
  //   performance estimator and module loops for you;
  //   do any selector-specific ones in your implementation
  //   of resetAfterInline)
  Pass &proxy = getPass();
  ModuleLoops &mloops = proxy.getAnalysis< ModuleLoops >();
  LoopProfLoad &loopprof = proxy.getAnalysis< LoopProfLoad >();
  UpdateEdgeLoopProfilers edgeloop( proxy, loopprof );

  bool changed = false;
  for(LateInliningOpportunities::const_iterator i=opportunities.begin(), e=opportunities.end(); i!=e; ++i)
  {
    Instruction *callsite = *i;
    CallSite cs = getCallSite( callsite );
    assert( cs.getInstruction() && "How did a non-callsite get into this list?");
    Function *callee = cs.getCalledFunction();
    BasicBlock *bb = callsite->getParent();
    Function *caller = bb->getParent();

    errs() << "Inlining callsite: " << *callsite << '\n'
           << " (in " << caller->getName() << " :: " << bb->getName() << ")\n";

    // - Perform inlining.
    InlineFunctionInfo ifi;
    ValueToValueMapTy vmap;
    CallsPromotedToInvoke call2invoke;
    if( !InlineFunctionWithVmap(cs, ifi, vmap, call2invoke) )
    {
      errs() << "  ==> didn't inline?!";
      continue; // Didn't inline for one reason or another
    }

    changed = true;
    ++numLateInlineFunctions;

    // Edge, Loop Profilers, Performance estimator,
    // and ModuleLoops are used by ALL selectors
    edgeloop.resetAfterInline(callsite, caller,callee,vmap, call2invoke);
    perf->reset();
    mloops.forget( caller );

    // Reset those analyses/profiles specific to this
    // subclass of selector:
    resetAfterInline(callsite, caller, callee, vmap, call2invoke);
  }
  errs() << "}}" << "}\n";

  return changed;
}


bool Selector::doSelection(
  Vertices &vertices,
  Edges &edges,
  VertexWeights &weights,
  VertexSet &maxClique)
{
  // We want to find the set of loops s.t.
  //  (1) All loops are parallelizable.
  //  (2) The loops can be compatibly parallelized
  //  (3) With the maximum overall expected reduction in execution time.

  // We do this via a reduction to the maximum weighted clique problem (MWCP).
  // Loop => vertex; Compatibly parallelized => edge; Expected reduction => weight.

  Pass &proxy = getPass();
  LoopProfLoad &lpl = proxy.getAnalysis< LoopProfLoad >();
  perf = &proxy.getAnalysis< ProfilePerformanceEstimator >();


  // Identify all classified loops as vertices
  unsigned numApplicable = 0;


  // twoh - scaledweights
  //
  // Theoritically, lpl.getLoopTime(header) should have an identical value as
  // perf.estimate_loop_weight(loop), because basically they try to estimate the same value
  // (execution time of a given loop).
  //
  // However, I see a case that perf.estimate_loop_weight(loop) has greater value than
  // lpl.getLoopTime(header). It seems that the difference comes from the way that
  // ProfilerPerformanceEstimator uses loop profiling result. I didn't pinpoint the reason
  // behind this.
  //
  // weights[i] values are computed in computeWeights using ProfilerPerformanceEstimator, thus
  // comparing those values with the values computed by LoopProfLoad is not sound. To make
  // weights[i] and lpl.getLoopTime(header) share the same baseline, computeWeights function
  // computes scaledweights as well.

  VertexWeights scaledweights;
  for(unsigned round=0; round<LateInlineMaxRounds; ++round)
  {
    errs() << "---------------------------------------------------- "
           << "Round " << (round+1)
           << " ----------------------------------------------------\n";

    computeVertices(vertices);
    if( vertices.empty() )
      return false;

    // Identify compatibilities among the loops as edges
    // computeEdges(vertices, edges);

    auto printCompatibleMap = [](Edges edges) {
      errs() << "Compatible Map:\n";
      for (auto &[p1, p2] : edges) {
        errs() << p1 << " " << p2 << "\n";
      }
      errs() << "End of Compatible Map\n";
    };
    printCompatibleMap(edges);

    // Identify edge weights.  Bigger weight is better.
    LateInliningOpportunities opportunities;

    // actually try to parallelize
    numApplicable = computeWeights(vertices, edges, weights, scaledweights, opportunities);

    REPORT_DUMP(summarizeParallelizableLoops(vertices,scaledweights,numApplicable));

    if( opportunities.empty() )
      break; // We don't see any opportunity for late inlining
    if( round + 1 >= LateInlineMaxRounds )
      break; // We won't try again

    ++numLateInlineRounds;

    if( !doInlining(opportunities) )
      break;

    // ...and try again
    vertices.clear();
    edges.clear();
    weights.clear();
    scaledweights.clear();

    //for(Loop2Strategy::iterator i=strategies.begin(), e=strategies.end(); i!=e; ++i)
    //  delete i->second;
    strategies.clear();
  }

  if( numApplicable < 1 )
    return false;

  // At this point, we have the graph (V,E) with weights.
  // Compute the maximum clique.  The max clique
  // are the loops we have selected.
  const int wt = ebk(edges, scaledweights, maxClique);

  REPORT_DUMP(
    const unsigned tt = lpl.getTotTime();
    const double speedup = tt / (tt - wt/(double)FixedPoint);
    errs() << "  Total expected speedup: " << format("%.2f", speedup) << "x using " << NumThreads << " workers.\n";
  );

  if( wt < 1 && ! IgnoreExpectedSpeedup )
    maxClique.clear();

  if( ParallelizeAtMostOneLoop && maxClique.size() > 1 )
    // Ensure that at most one loop is selected for parallelization.
    maxClique.resize(1);

  // Delete from strategies[] all loops that were not selected (i.e.
  // those which do not appear in maxClique).
  // let toDelete = (all vertices) - (max clique)
  Vertices toDelete( vertices );
  for(VertexSet::iterator i=maxClique.begin(), e=maxClique.end(); i!=e; ++i)
  {
    const unsigned v = *i;
    Loop *loop = vertices[ v ];

    auto *strat = strategies[loop->getHeader()].get();
    populateRemedCostPerStage(strat, loop,
                              *selectedRemedies[loop->getHeader()]);

    // 'loop' is a loop we will parallelize
    // if (DebugFlag &&
    //     (isCurrentDebugType(DEBUG_TYPE) || isCurrentDebugType("classify")))

    REPORT_DUMP(printOneLoopStrategy(errs(), loop, strategies[loop->getHeader()].get(),
                           lpl, true, *perf));

    Vertices::iterator j = std::find(toDelete.begin(), toDelete.end(), loop);
    if( j != toDelete.end() )
    {
      *j = toDelete.back();
      toDelete.pop_back();
    }
  }
  for(unsigned i=0, N=toDelete.size(); i<N; ++i)
  {
    Loop *deleteme = toDelete[i];
    if(selectedRemedies[deleteme->getHeader()])
    {
      auto *strat = strategies[deleteme->getHeader()].get();
      populateRemedCostPerStage(strat, deleteme,
                              *selectedRemedies[deleteme->getHeader()]);
    }

    // 'deleteme' is a loop we will NOT parallelize.
    REPORT_DUMP(printOneLoopStrategy(errs(), deleteme,
                           strategies[deleteme->getHeader()].get(), lpl, false,
                           *perf));

    Loop2Strategy::iterator j = strategies.find( deleteme->getHeader() );
    if( j != strategies.end() )
    {
      //if( j->second )
        //delete j->second;
      strategies.erase(j);
    }
    selectedLoops.erase(deleteme->getHeader());
  }

  numSelected += maxClique.size();
  return true;
}

char Selector::ID = 0;
static RegisterAnalysisGroup< Selector > group("Parallelization selector");
}
