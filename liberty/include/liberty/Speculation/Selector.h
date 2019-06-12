// Given a set of hot loops with assignments,
// decide which loops to parallelize, and
// form a compatible assignment.
#ifndef LLVM_LIBERTY_SPEC_PRIV_SELECTOR_H
#define LLVM_LIBERTY_SPEC_PRIV_SELECTOR_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/CallGraph.h"

#include <vector>
#include <set>
#include <unordered_set>
#include <map>

#include "liberty/Utilities/InlineFunctionWithVmap.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/GraphAlgorithms/Graphs.h"
#include "liberty/Speculation/UpdateOnClone.h"
#include "liberty/Strategy/PerformanceEstimator.h"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "liberty/Strategy/PipelineStrategy.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/Orchestration/Orchestrator.h"
#include "liberty/Orchestration/Remediator.h"

namespace liberty
{
namespace SpecPriv
{

class HeapAssignment;

void printOneLoopStrategy(raw_ostream &fout,
  Loop *loop, LoopParallelizationStrategy *strategy,
  LoopProfLoad &lpl, bool willTransform);

struct Selector : public UpdateOnClone
{
  static char ID;
  Selector() {}
  virtual ~Selector();

  static const unsigned FixedPoint;
  static const unsigned PenalizeLoopNest;
  static const unsigned NumThreads;

  const LoopParallelizationStrategy &getStrategy(Loop *loop) const;
  LoopParallelizationStrategy &getStrategy(Loop *loop);

  //typedef std::map<BasicBlock*, LoopParallelizationStrategy*> Loop2Strategy;
  typedef std::map<BasicBlock *, std::unique_ptr<LoopParallelizationStrategy>>
      Loop2Strategy;
  typedef Loop2Strategy::const_iterator strat_iterator;

  typedef std::map<BasicBlock *, std::unique_ptr<SelectedRemedies>>
      Loop2SelectedRemedies;
  typedef std::map<BasicBlock *, Critic_ptr> Loop2SelectedCritics;
  typedef std::map<BasicBlock *, std::unique_ptr<LoopDependenceInfo>>
      Loop2DepInfo;

  strat_iterator strat_begin() const { return strategies.begin(); }
  strat_iterator strat_end() const { return strategies.end(); }

  typedef std::unordered_set<BasicBlock *> SelectedLoops;
  typedef SelectedLoops::iterator sloops_iterator;
  sloops_iterator sloops_begin() { return selectedLoops.begin(); }
  sloops_iterator sloops_end() { return selectedLoops.end(); }

  Loop2DepInfo &getLoop2DepInfo() { return loopDepInfo; }
  Loop2SelectedRemedies &getLoop2SelectedRemedies() { return selectedRemedies; }

  ProfilePerformanceEstimator *getProfilePerformanceEstimator() {
    return perf;
  };

  virtual const HeapAssignment &getAssignment() const;
  virtual HeapAssignment &getAssignment();

  // Isn't multiple inheritance wonderful!?
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI)
  {
    if(PI == &Selector::ID)
      return (Selector*)this;
    return this;
  }

  typedef std::vector<Loop*> Vertices;
  /// Represents a set of callsites that, if inlined, might improve
  /// the pipeline partition.
  typedef std::set< Instruction* > LateInliningOpportunities;

  // Update on clone
  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap);

private:
  typedef std::unordered_map<const Loop *, std::unordered_set<const Function *>>
      LoopToTransCalledFuncs;
  static bool callsFun(const Loop *l, const Function *tgtF,
                       LoopToTransCalledFuncs &l2cF, CallGraph &callGraph);

  void computeEdges(const Vertices &vertices, Edges &edges);
  static bool mustBeSimultaneouslyActive(const Loop *A, const Loop *B,
                                         LoopToTransCalledFuncs &l2cF,
                                         CallGraph &callGraph);
  bool doInlining(LateInliningOpportunities &opps);
  // Reduction into maximum weighted clique problem
  unsigned computeWeights(
    const Vertices &vertices, // input
    Edges &Edges, // input/output
    VertexWeights &weights, // output
    LateInliningOpportunities &opportunities // output
    );
  void summarizeParallelizableLoops(const Vertices &vertices, const VertexWeights &weights, unsigned numApplicable);
  /// Analyze a pipeline strategy to find opportunities for late inlining.
  static void findLateInliningOpportunities(
    Loop *loop,
    const PipelineStrategy &strat,
    PerformanceEstimator &perf,
    // output
    LateInliningOpportunities &opps);
  static void findLateInliningOpportunities(
    Loop *loop,
    const PipelineStage &sequential,
    PerformanceEstimator &perf,
    // output
    LateInliningOpportunities &opps);
  static void findLateInliningOpportunities(
    Loop *loop,
    const PipelineStage::ISet &iset,
    const unsigned stage_weight,
    PerformanceEstimator &perf,
    // output
    LateInliningOpportunities &opps);

protected:
  Loop2Strategy strategies;

  Loop2SelectedRemedies selectedRemedies;
  Loop2SelectedCritics selectedCritics;
  Loop2DepInfo loopDepInfo;

  SelectedLoops selectedLoops;

  ProfilePerformanceEstimator *perf;

  bool doSelection(
    Vertices &vertices,
    Edges &edges,
    VertexWeights &weights,
    VertexSet &maxClique);
  static void analysisUsage(AnalysisUsage &au);

  // --------------- Subclasses should overload these ------------------------

  // particular pipelining options?
  virtual bool pipelineOption_ignoreAntiOutput() const { return false; }
  virtual bool pipelineOption_constrainSubLoops() const { return false;  }
  virtual bool pipelineOption_includeReplicableStages() const { return true; }
  virtual bool pipelineOption_abortIfNoParallelStage() const { return true; }
  virtual bool pipelineOption_includeParallelStages() const { return true;  }

  // What kind of speculation to use?
  // speculation on control deps:
  virtual ControlSpeculation *getControlSpeculation() const;
  // speculation on register deps:
  virtual PredictionSpeculation *getPredictionSpeculation() const;
  // speculation on memory deps:
  virtual void buildSpeculativeAnalysisStack(const Loop *A) {}
  virtual void destroySpeculativeAnalysisStack() {}

  // Called after parallelization strategies are identified.
  // Used to prevent simultanoeous selection of two loops.
  // Note that the default will automatically prevent
  // parallelizing two loops which MUST be simultaneously active.
  virtual bool compatibleParallelizations(const Loop *A, const Loop *B) const { return true; }

  // By default, select from loops according to the hot loop identifier
  virtual void computeVertices(Vertices &vertices);

  unsigned computeWeights(
    const Vertices &vertices,
    Edges &edges,
    VertexWeights &weights,
    VertexWeights &scaledweights,
    LateInliningOpportunities &opportunities);

  // Because the type system.
  virtual Pass &getPass() = 0;

  // Called in response to a late inlining.
  // Useful to clean-up/patch/repair any analyses/profiles
  // affected by that inlining.
  virtual void resetAfterInline(
    Instruction *callsite_no_longer_exists,
    Function *caller,
    Function *callee,
    const ValueToValueMapTy &vmap,
    const CallsPromotedToInvoke &call2invoke) {}
};

}
}

#endif

