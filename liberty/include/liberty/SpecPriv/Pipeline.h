// A Speculative PS-DSWP transform.
#ifndef LLVM_LIBERTY_SPEC_PRIV_TRANSFORM_PIPELINE_H
#define LLVM_LIBERTY_SPEC_PRIV_TRANSFORM_PIPELINE_H

#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/DAGSCC.h"
#include "liberty/SpecPriv/PDG.h"
#include "liberty/SpecPriv/PipelineStrategy.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct PerformanceEstimator;

struct Pipeline
{
  static bool isApplicable(
    // Inputs
    Loop *loop,
    LoopAA *aa,
    ControlSpeculation &ctrlspec,
    PredictionSpeculation &predspec,
    PerformanceEstimator &perf,
    // Optional inputs
    unsigned threadBudget = 25,
    bool ignoreAntiOutput = false,
    bool includeReplicableStages = true,
    bool constrainSubLoops = false,
    bool abortIfNoParallelStage = true,
    bool includeParallelStages = true);

  /// This is the preferred method of computing a PS-DSWP pipeline
  static bool suggest(
    // Inputs
    Loop *loop,
    LoopAA *aa,
    ControlSpeculation &ctrlspec,
    PredictionSpeculation &predspec,
    PerformanceEstimator &perf,
    // Output
    PipelineStrategy &strat,
    // Optional inputs
    unsigned threadBudget = 25,
    bool ignoreAntiOutput = false,
    bool includeReplicableStages = true,
    bool constrainSubLoops = false,
    bool abortIfNoParallelStage = true,
    bool includeParallelStages = true);

  /// This is public only for debugging purposes.
  /// Avoid using it.
  static bool suggest(
    // Inputs
    Loop *loop,
    const PDG &pdg,
    SCCs &sccs,
    PerformanceEstimator &perf,
    // Output
    PipelineStrategy &strat,
    // Optional inputs
    unsigned threadBudget = 25,
    bool includeReplicableStages = true,
    bool abortIfNoParallelStage = true,
    bool includeParallelStages = true);

private:

  // Recursively decompose the DAG-SCC into a pipeline
  // Root case
  static bool doallAndPipeline(
    // Inputs
    const PDG &pdg, const SCCs &sccs,
    PerformanceEstimator &perf, ControlSpeculation &ctrlspec,
    // Output
    PipelineStrategy::Stages &stages,
    // Inputs
    unsigned threadBudget, bool includeReplicableStages = true, bool includeParallelStages = true);
  // Recursive case
  static bool doallAndPipeline(
    // Inputs
    const PDG &pdg, const SCCs &sccs, SCCs::SCCSet &all_sccs,
    PerformanceEstimator &perf, ControlSpeculation &ctrlspec,
    // Outputs
    PipelineStrategy::Stages &stages, unsigned long &score_out, unsigned &numThreadsUsed_out,
    // Inputs
    unsigned threadBudget, bool includeReplicableStages = true, bool includeParallelStages = true);

  // Perform greedy DSWP paritioning (not PS-DSWP partitioning)
  // Per the heuristic in Ottoni's thesis.
  static bool greedyDSWP(
    // Inputs
    const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all_sccs,
    PerformanceEstimator &perf, ControlSpeculation &ctrlspec,
    // Outputs
    PipelineStrategy::Stages &stages, unsigned long &score_out, unsigned &numThreadsUsed_out,
    // Inputs
    unsigned threadBudget);

  // Partition 'all' into three sets 'before', 'after', and 'flexible' according to whether
  // or not each SCC in 'all' must precede/follow any SCC in 'pivot'
  static void pivot(const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all, const SCCs::SCCSet &pivots,
    SCCs::SCCSet &before, SCCs::SCCSet &after, SCCs::SCCSet &flexible);

  // Same as previous, but redirect 'flexible' into 'after'
  static void pivot(const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all, const SCCs::SCCSet &pivots,
    SCCs::SCCSet &before, SCCs::SCCSet &after);

  // Find the largest set of DOALL SCCs 'good_sccs' from 'all_sccs' such that there is no cycle between
  // 'good_sccs' and 'bad_sccs'
  static bool findMaxParallelStage(
    // Inputs
    const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all_sccs,
    PerformanceEstimator &perf, ControlSpeculation &ctrlspec,
    bool includeReplicableStages,
    // Outputs
    SCCs::SCCSet &good_sccs, SCCs::SCCSet &bad_sccs);

  // Find the largest set of Replicable SCCs 'good_sccs' from 'all_sccs' such that there is no cycle between
  // 'good_sccs' and 'bad_sccs'
  static bool findMaxReplicableStage(
    // Inputs
    const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all_sccs,
    PerformanceEstimator &perf,
    // Outputs
    SCCs::SCCSet &good_sccs, SCCs::SCCSet &bad_sccs);

  // Find the largest set of SCCs 'good_sccs' that satisfy 'pred' such that there is no cycle between
  // 'good_sccs' and 'bad_sccs'
  template <class Predicate, class Relation>
  static bool findMaxGoodStage(
    // Inputs
    const PDG &pdg,
    const SCCs &sccs,
    const SCCs::SCCSet &all_sccs,
    PerformanceEstimator &perf,
    const Predicate &is_good_scc,
    const Relation &incompatibility_among_sccs,
    // Ouputs
    SCCs::SCCSet &good_sccs, SCCs::SCCSet &bad_sccs);
};

}
}

#endif
