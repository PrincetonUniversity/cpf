#define DEBUG_TYPE "selector"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/ADT/GraphTraits.h"
#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/DomPrinter.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/DOTGraphTraits.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/Selector.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Strategy/PipelineStrategy.h"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "PDG.hpp"
#include "liberty/PDGBuilder/PDGBuilder.hpp"

#include "liberty/GraphAlgorithms/Ebk.h"
//#include "PtrResidueAA.h"
//#include "LocalityAA.h"
//#include "RoI.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/UpdateOnCloneAdaptors.h"
#include "liberty/Speculation/HeaderPhiPredictionSpeculation.h"
//#include "Transform.h"
#include "liberty/Orchestration/LocalityAA.h"

#include "LoopDependenceInfo.hpp"
#include "DGGraphTraits.hpp"

using namespace llvm;

namespace liberty
{
namespace SpecPriv
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
  //au.addRequired< DataLayout >();
  au.addRequired< TargetLibraryInfoWrapperPass >();
  //au.addRequired< ProfileInfo >();
  au.addRequired< BlockFrequencyInfoWrapperPass >();
  au.addRequired< BranchProbabilityInfoWrapperPass >();
  //au.addRequired< LoopAA >();
  au.addRequired< PDGBuilder >();
  au.addRequired< ModuleLoops >();
  au.addRequired< LoopProfLoad >();
  au.addRequired< Targets >();
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

template <class GT>
void writeGraph(const std::string &filename, GT *graph) {
  std::error_code EC;
  raw_fd_ostream File(filename, EC, sys::fs::F_Text);
  std::string Title = DOTGraphTraits<GT *>::getGraphName(graph);

  if (!EC) {
    WriteGraph(File, graph, false, Title);
  } else {
    DEBUG(errs() << "Error opening file for writing!\n");
    abort();
  }
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

unsigned Selector::computeWeights(
  const Vertices &vertices,
  Edges &edges,
  VertexWeights &weights,
  VertexWeights &scaledweights,
  LateInliningOpportunities &opportunities)
{
  unsigned numApplicable = 0;

  //ControlSpeculation *ctrlspec = getControlSpeculation();
  //PredictionSpeculation *predspec = getPredictionSpeculation();

  Pass &proxy = getPass();
  ProfilePerformanceEstimator &perf = proxy.getAnalysis< ProfilePerformanceEstimator >();
  LoopProfLoad &lpl = proxy.getAnalysis< LoopProfLoad >();
  PDGBuilder &pdgBuilder = proxy.getAnalysis< PDGBuilder >();
  ModuleLoops &mloops = proxy.getAnalysis< ModuleLoops >();
  ControlSpeculation *ctrlspec =
      proxy.getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
  PredictionSpeculation *headerPhiPred =
      &proxy.getAnalysis<HeaderPhiPredictionSpeculation>();
  PredictionSpeculation *loadedValuePred =
      &proxy.getAnalysis<ProfileGuidedPredictionSpeculator>();
  SmtxSlampSpeculationManager &smtxMan =
      proxy.getAnalysis<SmtxSlampSpeculationManager>();
  SmtxSpeculationManager &smtxLampMan =
      proxy.getAnalysis<SmtxSpeculationManager>();
  const Read &rd = proxy.getAnalysis<ReadPass>().getProfileInfo();
  Classify &classify = proxy.getAnalysis<Classify>();
  LoopAA *loopAA = proxy.getAnalysis<LoopAA>().getTopAA();

  const unsigned N = vertices.size();
  weights.resize(N);
  scaledweights.resize(N);
  for(unsigned i=0; i<N; ++i)
  {
    Loop *A = vertices[i];
    BasicBlock *hA = A->getHeader();
    Function *fA = hA->getParent();
    //const Twine nA = fA->getName() + " :: " + hA->getName();

    DEBUG(errs()
          << "\n\n=--------------------------------------------------------"
             "----------------------=\nCompute weight for loop "
          << fA->getName() << " :: " << hA->getName() << "...\n");

    LoopInfo &li = mloops.getAnalysis_LoopInfo(fA);
    PostDominatorTree &pdt = mloops.getAnalysis_PostDominatorTree(fA);
    ScalarEvolution &se = mloops.getAnalysis_ScalarEvolution(fA);

    const HeapAssignment &asgn = classify.getAssignmentFor(A);

    const unsigned long loopTime = perf.estimate_loop_weight(A);
    const unsigned long scaledLoopTime = FixedPoint*loopTime;
    const unsigned depthPenalty = PenalizeLoopNest*A->getLoopDepth(); // break ties with nested loops

    unsigned long adjLoopTime = scaledLoopTime;
    if( scaledLoopTime > depthPenalty )
      adjLoopTime = scaledLoopTime - depthPenalty;
    else if (scaledLoopTime > depthPenalty / 10)
      adjLoopTime = scaledLoopTime - depthPenalty / 10;

    {
      //std::unique_ptr<llvm::PDG> pdg = pdgBuilder.getLoopPDG(A);
      llvm::PDG *pdg = pdgBuilder.getLoopPDG(A).release();

      std::string pdgDotName = "pdg_" + hA->getName().str() + "_" + fA->getName().str() + ".dot";
      writeGraph<PDG>(pdgDotName, pdg);

      std::unique_ptr<LoopDependenceInfo> ldi =
          std::make_unique<LoopDependenceInfo>(pdg, A, li, pdt);

      ldi->sccdagAttrs.populate(ldi->loopSCCDAG, ldi->liSummary, se);

      // trying to find the best parallelization strategy for this loop

      DEBUG(
          errs() << "Run Orchestrator:: find best parallelization strategy for "
                 << fA->getName() << " :: " << hA->getName() << "...\n");

      std::unique_ptr<Orchestrator> orch =
          std::unique_ptr<Orchestrator>(new Orchestrator());

      std::unique_ptr<PipelineStrategy> ps;
      std::unique_ptr<SelectedRemedies> sr;
      Critic_ptr sc;

      bool applicable = orch->findBestStrategy(
          A, *pdg, *ldi, perf, ctrlspec, loadedValuePred, headerPhiPred, mloops,
          smtxMan, smtxLampMan, rd, asgn, proxy, loopAA, lpl, ps, sr, sc,
          NumThreads, pipelineOption_ignoreAntiOutput(),
          pipelineOption_includeReplicableStages(),
          pipelineOption_constrainSubLoops(),
          pipelineOption_abortIfNoParallelStage());

      if( applicable )
      {
        ++numApplicable;
        ps->setValidFor( hA );

        unsigned long  estimatePipelineWeight = (long) FixedPoint*perf.estimate_pipeline_weight(*ps, A);
        const long wt = adjLoopTime - estimatePipelineWeight;
        unsigned long scaledwt = 0;

        //errs() << "wt: " << wt << "\nadjLoopTime: " << adjLoopTime
        //       << "\nestimatePipelineWeight: " << estimatePipelineWeight
        //       << "\ndepthPenalty: " << depthPenalty << '\n';

        //ps->dump_pipeline(errs());

        if (perf.estimate_loop_weight(A))
          scaledwt = wt * (double)lpl.getLoopTime(hA) / (double)perf.estimate_loop_weight(A);

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

        DEBUG(errs() << "Parallelizable Loop " << fA->getName()
                     << " :: " << hA->getName() << " has expected savings "
                     << weights[i] << '\n');

        findLateInliningOpportunities(A,*ps,perf,opportunities);

        strategies[ hA ] = std::move(ps);
        selectedRemedies[ hA ] = std::move(sr);
        selectedCritics[ hA ] = sc;
        loopDepInfo [hA] = std::move(ldi);
        selectedLoops.insert(hA);

      } else {
        DEBUG(errs() << "No parallelizing transform applicable to "
                     << fA->getName() << " :: " << hA->getName() << '\n';);

        weights[i] = 0;
        scaledweights[i] = 0;
        for (unsigned v = 0; v < N; ++v) {
          edges.erase(Edge(i, v));
          edges.erase(Edge(v, i));
        }
      }
    }
  }

  return numApplicable;
}

void getCalledFuns(CallGraphNode *cgNode,
                   unordered_set<const Function *> &calledFuns) {
  for (auto i = cgNode->begin(), e = cgNode->end(); i != e; ++i) {
    auto *succ = i->second;
    auto *F = succ->getFunction();
    if (!F || calledFuns.count(F) || F->isDeclaration())
      continue;
    calledFuns.insert(F);
    getCalledFuns(succ, calledFuns);
  }
}

bool Selector::callsFun(const Loop *l, const Function *tgtF,
                        LoopToTransCalledFuncs &loopTransCallGraph,
                        CallGraph &callGraph) {
  if (loopTransCallGraph.count(l))
    return loopTransCallGraph[l].count(tgtF);

  for (const BasicBlock *BB : l->getBlocks()) {
    for (const Instruction &I : *BB) {
      const CallInst *call = dyn_cast<CallInst>(&I);
      if (!call)
        continue;
      const Function *cFun = call->getCalledFunction();
      if (!cFun || cFun->isDeclaration())
        continue;
      auto *cgNode = callGraph[cFun];
      loopTransCallGraph[l].insert(cFun);
      getCalledFuns(cgNode, loopTransCallGraph[l]);
    }
  }
  return loopTransCallGraph[l].count(tgtF);
}

bool Selector::mustBeSimultaneouslyActive(
    const Loop *A, const Loop *B, LoopToTransCalledFuncs &loopTransCallGraph,
    CallGraph &callGraph) {

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
  auto &callGraph = proxy.getAnalysis<CallGraphWrapperPass>().getCallGraph();
  for(unsigned i=0; i<N; ++i)
  {
    Loop *A = vertices[i];

    BasicBlock *hA = A->getHeader();
    Function *fA = hA->getParent();
    //const Twine nA = fA->getName() + " :: " + hA->getName();

    for(unsigned j=i+1; j<N; ++j)
    {
      Loop *B = vertices[j];

      BasicBlock *hB = B->getHeader();
      Function *fB = hB->getParent();
      //const Twine nB = fB->getName() + " :: " + hB->getName();

      /* If we can prove simultaneous activation,
       * exclude one of the loops */
      if( mustBeSimultaneouslyActive(A, B, loopTransCallGraph,callGraph) )
      {
        DEBUG(errs() << "Loop " << fA->getName() << " :: " << hA->getName()
                     << " is incompatible with loop " << fB->getName()
                     << " :: " << hB->getName()
                     << " because of simultaneous activation.\n");
        continue;
      }

      if (!compatibleParallelizations(A, B)) {
        DEBUG(errs() << "Loop " << fA->getName() << " :: " << hA->getName()
                     << " is incompatible with loop " << fB->getName()
                     << " :: " << hB->getName()
                     << " because of incompatible assignments.\n");
        continue;
      }

      DEBUG(errs() << "Loop " << fA->getName() << " :: " << hA->getName()
                   << " is COMPATIBLE with loop " << fB->getName()
                   << " :: " << hB->getName() << ".\n");
      edges.insert(Edge(i, j));
      edges.insert( Edge(j,i) );
    }
  }
}

void printOneLoopStrategy(raw_ostream &fout, Loop *loop, LoopParallelizationStrategy *strategy, LoopProfLoad &lpl, bool willTransform)
{
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

  if( strategy )
    strategy->summary(fout);
  else
    fout << "(no strat)";

  if( willTransform )
    fout << "\t\t\t#regrn-par-loop\n";
  else
    fout << "\t\t\t#regrn-no-par-loop\n";
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
    errs() << "*********************************************************************\n"
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
  ProfilePerformanceEstimator &perf = proxy.getAnalysis< ProfilePerformanceEstimator >();
  //sot
  //ProfileInfo &edgeprof = proxy.getAnalysis< ProfileInfo >();
  // since BranchProbabilityInfo and BlockFrequencyInfo are FunctionPasses we cannot call them here.
  // need to point to specific functions. Call getAnalysis when resetAfterInline
  //BranchProbabilityInfo &bpi = proxy.getAnalysis< BranchProbabilityInfoWrapperPass >().getBPI();
  //BlockFrequencyInfo &bfi = proxy.getAnalysis< BlockFrequencyInfoWrapperPass >().getBFI();

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
    perf.reset();
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
    computeEdges(vertices, edges);

    // Identify edge weights.  Bigger weight is better.
    LateInliningOpportunities opportunities;
    numApplicable = computeWeights(vertices, edges, weights, scaledweights, opportunities);

    if( DebugFlag
    && (isCurrentDebugType(DEBUG_TYPE) || isCurrentDebugType("classify") ) )
      summarizeParallelizableLoops(vertices,scaledweights,numApplicable);

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

  if( DebugFlag
  && (isCurrentDebugType(DEBUG_TYPE) || isCurrentDebugType("classify") ) )
  {
    const unsigned tt = lpl.getTotTime();
    const double speedup = tt / (tt - wt/(double)FixedPoint);
    errs() << "  Total expected speedup: " << format("%.2f", speedup) << "x using " << NumThreads << " workers.\n";
  }

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

    // 'loop' is a loop we will parallelize
    if( DebugFlag && (isCurrentDebugType(DEBUG_TYPE) || isCurrentDebugType("classify") ) )
      printOneLoopStrategy(errs(), loop, strategies[loop->getHeader()].get(), lpl, true);

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

    // 'deleteme' is a loop we will NOT parallelize.
    if( DebugFlag && (isCurrentDebugType(DEBUG_TYPE) || isCurrentDebugType("classify") ) )
      printOneLoopStrategy(errs(), deleteme, strategies[deleteme->getHeader()].get(), lpl, false);

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
}
