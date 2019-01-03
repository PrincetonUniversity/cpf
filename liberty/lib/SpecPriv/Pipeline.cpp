#define DEBUG_TYPE "pipeline"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/ValueMap.h"

#include "liberty/SpecPriv/PDG.h"
#include "liberty/SpecPriv/PerformanceEstimator.h"
#include "liberty/SpecPriv/Pipeline.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/Timer.h"

#include "EdmondsKarp.h"
#include "Preprocess.h"

#include <iterator>
#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

static cl::opt<bool> PrintPDG(
  "specpriv-print-dag-scc", cl::init(false), cl::NotHidden,
  cl::desc("Print DAG-SCC-PDG for each hot loop"));
static cl::opt<bool> PrintPipeline(
  "specpriv-print-pipeline", cl::init(false), cl::NotHidden,
  cl::desc("Print pipeline for each hot loop"));
static cl::opt<bool> FengHack(
  "specpriv-feng-no-mem-in-replicable-stages", cl::init(false), cl::Hidden,
  cl::desc("Never place memory operations into replicable stages"));


static unsigned filename_nonce = 0;

static void printPDG(const Loop *loop, const PDG &pdg, const SCCs &sccs, bool bailout=false)
{
  if( !PrintPDG )
    return;
  const BasicBlock *header = loop->getHeader();
  const Function *fcn = header->getParent();

  std::string hname = header->getName();
  std::string fname = fcn->getName();

  ++filename_nonce;

  char fn[256];
  snprintf(fn,256,"pdg-%s-%s--%d.dot", fname.c_str(), hname.c_str(), filename_nonce);

  char fn2[256];
  snprintf(fn2,256,"pdg-%s-%s--%d.tred", fname.c_str(), hname.c_str(), filename_nonce);

  sccs.print_dot(pdg,fn,fn2,bailout);
  errs() << "See " << fn << " and " << fn2 << '\n';
}

static void printPipeline(const Loop *loop, const PDG &pdg, const SCCs &sccs, const PipelineStrategy &strat, ControlSpeculation *ctrlspec = 0)
{
  if( !PrintPipeline )
    return;
  const BasicBlock *header = loop->getHeader();
  const Function *fcn = header->getParent();

  std::string hname = header->getName();
  std::string fname = fcn->getName();

  ++filename_nonce;

  char fn[256];
  snprintf(fn,256,"pipeline-%s-%s--%d.dot", fname.c_str(), hname.c_str(), filename_nonce);

  char fn2[256];
  snprintf(fn2,256,"pipeline-%s-%s--%d.tred", fname.c_str(), hname.c_str(), filename_nonce);

  strat.print_dot(pdg,sccs,fn,fn2,ctrlspec);
  errs() << "See " << fn << " and " << fn2 << '\n';
}

static EdgeWeight estimate_weight(const PDG &pdg, const SCCs &sccs, PerformanceEstimator &perf, unsigned scc_id)
{
  EdgeWeight sum_weight = 0;

  const Vertices &V = pdg.getV();
  const SCCs::SCC &scc = sccs.get(scc_id);
  for(unsigned i=0, N=scc.size(); i<N; ++i)
  {
    Vertices::ID member = scc[i];
    Instruction *inst = V.get(member);

    sum_weight += perf.estimate_weight(inst);
  }

  return sum_weight;
}

static EdgeWeight estimate_weight(const PDG &pdg, const SCCs &sccs, PerformanceEstimator &perf, const PipelineStage &stage)
{
  return perf.estimate_weight( stage.instructions.begin(), stage.instructions.end() );
}

static EdgeWeight estimate_weight(const PDG &pdg, const SCCs &sccs, PerformanceEstimator &perf, const SCCs::SCCSet &scc_set)
{
  EdgeWeight sum_weight = 0;

  for(unsigned i=0, N=scc_set.size(); i<N; ++i)
    sum_weight += estimate_weight(pdg,sccs,perf, scc_set[i]);

  return sum_weight;
}

bool Pipeline::suggest(Loop *loop, LoopAA *loopAA, ControlSpeculation &ctrlspec,
                       PredictionSpeculation &predspec,
                       PerformanceEstimator &perf, PipelineStrategy &strat,
                       unsigned threadBudget, bool ignoreAntiOutput,
                       bool includeReplicableStages, bool constrainSubLoops,
                       bool abortIfNoParallelStage,
                       bool includeParallelStages) {
  Vertices vertices(loop);

  errs() << "\t*** start building PDG, " << loop->getHeader()->getName().str() << "\n";
  errs().flush();

  PDG pdg(vertices, ctrlspec,predspec,loopAA->getDataLayout(),ignoreAntiOutput,constrainSubLoops);
  pdg.setAA(loopAA);

  errs() << "\t*** start building scc, " << loop->getHeader()->getName().str() << "\n";
  errs().flush();

  SCCs sccs( pdg );

  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  errs() << "\t*** start computeDagScc\n";
  errs().flush();

  const bool pstage = SCCs::computeDagScc(pdg,sccs,abortIfNoParallelStage);

  errs() << "\t*** start printPDG\n";
  errs().flush();

  printPDG(loop,pdg,sccs);

  //if( !pstage && !pstageAfterRemedies)
  if( !pstage )
  {
    DEBUG(errs() << "PS-DSWP not applicable to " << fcn->getName() << "::" << header->getName()
           << ": no non-trivial parallel stage (1)\n");

    // Return a degenerate DSWP[S] partition of this loop
    strat.stages.push_back( PipelineStage(loop) );
    return false;
  }
  errs() << "\t*** DAG is complete\n";
  errs().flush();
  // DAG is complete.

  bool success = suggest(loop,pdg,sccs,perf,strat,threadBudget,includeReplicableStages, abortIfNoParallelStage, includeParallelStages);

  return success;
}

bool Pipeline::suggest(
  Loop *loop,
  const PDG &pdg,
  SCCs &sccs,
  PerformanceEstimator &perf,
  PipelineStrategy &strat,
  unsigned threadBudget,
  bool includeReplicableStages,
  bool abortIfNoParallelStage,
  bool includeParallelStages)
{
  sccs.computeReachabilityAmongSCCs(pdg);

  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  const Vertices &vertices = pdg.getV();
  ControlSpeculation &ctrlspec = pdg.getControlSpeculator();

  if( ! doallAndPipeline(pdg,sccs,perf,ctrlspec, strat.stages, threadBudget, includeReplicableStages, includeParallelStages) )
  {
    DEBUG(errs() << "PS-DSWP not applicable to " << fcn->getName() << "::" << header->getName()
           << ": no parallelism found (2)\n");
    return false;
  }

  strat.setValidFor(loop->getHeader());
  strat.assertConsistentWithIR(loop);
  strat.assertPipelineProperty(pdg);

  if( strat.expandReplicatedStages() )
  {
    strat.assertConsistentWithIR(loop);
    strat.assertPipelineProperty(pdg);
  }

  PartialEdge filter_control_dependence = PartialEdge();
  filter_control_dependence.ii_ctrl = filter_control_dependence.lc_ctrl = true;

  // Compute the set of control dependences which
  // span pipeline stages.
  const PartialEdgeSet &pes = pdg.getE();
  // Foreach stage
  for(unsigned i=0, N=strat.stages.size(); i<N; ++i)
  {
    const PipelineStage &si = strat.stages[i];
    // Foreach instruction src in that stage that may source control deps
    for(PipelineStage::ISet::iterator j=si.instructions.begin(), z=si.instructions.end(); j!=z; ++j)
    {
      Instruction *src = *j;
      if( ! isa<TerminatorInst>(src) )
        continue;

      Vertices::ID vsrc = vertices.get(src);
      // Foreach control-dep successor of src
      for(PartialEdgeSet::iterator l=pes.successor_begin(vsrc, filter_control_dependence), Z=pes.successor_end(vsrc); l!=Z; ++l)
      {
        Vertices::ID vdst = *l;
        Instruction *dst = vertices.get(vdst);

        // That spans stages.
//        if( !si.instructions.count(dst) )
          strat.crossStageDeps.push_back( CrossStageDependence(src,dst, pes.find(vsrc,vdst) ) );
      }
    }
  }

  DEBUG(strat.dump_pipeline(errs(), &ctrlspec));
  printPipeline(loop,pdg,sccs,strat, &ctrlspec);

  return true;
}

void Pipeline::pivot(const PDG &pdg, const SCCs &sccs,
  const SCCs::SCCSet &all, const SCCs::SCCSet &pivots,
  SCCs::SCCSet &before, SCCs::SCCSet &after)
{
  SCCs::SCCSet flexible;

  pivot(pdg,sccs,all,pivots,before,after,flexible);

  // Favor shorter pipelines
  if( after.empty() )
    before.insert(before.end(),
      flexible.begin(), flexible.end());
  else
    after.insert(after.end(),
      flexible.begin(), flexible.end());
}

void Pipeline::pivot(const PDG &pdg, const SCCs &sccs,
  const SCCs::SCCSet &all, const SCCs::SCCSet &pivots,
  SCCs::SCCSet &before, SCCs::SCCSet &after, SCCs::SCCSet &flexible)
{
  for(SCCs::SCCSet::const_iterator i=all.begin(), e=all.end(); i!=e; ++i)
  {
    int scc_id = *i;

    // Does scc come before any of the SCCs in maxParallel?
    if( sccs.orderedBefore(scc_id, pivots) )
      before.push_back(scc_id);

    else if( sccs.orderedBefore(pivots, scc_id) )
      after.push_back(scc_id);

    else
      flexible.push_back(scc_id);
  }
}

bool Pipeline::doallAndPipeline(
  const PDG &pdg, const SCCs &sccs,
  PerformanceEstimator &perf, ControlSpeculation &ctrlspec,
  PipelineStrategy::Stages &stages, unsigned threadBudget, bool includeReplicableStages,
  bool includeParallelStages)
{
  // Set of all SCCs
  const unsigned N = sccs.size();
  SCCs::SCCSet all_sccs(N);
  for(unsigned i=0; i<N; ++i)
    all_sccs[i] = i;

  unsigned long score;
  unsigned numThreadsUsed;

  const bool res = doallAndPipeline(pdg,sccs,all_sccs,perf,ctrlspec,
    stages,
    score, numThreadsUsed,
    threadBudget,includeReplicableStages, includeParallelStages);

  if( stages.size() == 1
  &&  stages[0].type != PipelineStage::Parallel )
    return false;

  return res;
}

static bool includes_parallel_scc(const SCCs &sccs, const SCCs::SCCSet &set)
{
  for(unsigned i=0, N=set.size(); i<N; ++i)
  {
    unsigned scc_num = set[i];
    const SCCs::SCC &scc = sccs.get(scc_num);
    if( !sccs.mustBeInSequentialStage(scc) )
      return true;
  }

  return false;
}


// See Section 4.5 of Easwaran's dissertation.
bool Pipeline::doallAndPipeline(
  const PDG &pdg, const SCCs &sccs,
  SCCs::SCCSet &all_sccs,
  PerformanceEstimator &perf, ControlSpeculation &ctrlspec,
  PipelineStrategy::Stages &stages,
  unsigned long &score_out, unsigned &numThreads_out,
  unsigned threadBudget, bool includeReplicableStages, bool includeParallelStages)
{
  if( all_sccs.empty() )
  {
    score_out = ~(0ul);
    numThreads_out = 0;
    return false;
  }

  assert( threadBudget > 0 && "Can't schedule >0 SCCs with <1 threads");

  // This recursive case comes directly from Easwaran.
  // Build a pipline: {before} DOALL {after}
  SCCs::SCCSet maxParallel, notMaxParallel;
  if( includeParallelStages && findMaxParallelStage(pdg,sccs,all_sccs,perf,ctrlspec,includeReplicableStages,maxParallel,notMaxParallel) )
  {
    SCCs::SCCSet before, after;
    pivot(pdg,sccs, notMaxParallel, maxParallel, before, after);

    // Easwaran's dissertation does this nasty n**2 brute-force thing...

    // Determine inclusive lower/upper bounds for the number
    // of threads to assign to each of these three sets.
    const int min_before = before.empty() ? 0 : 1;
    const int min_par    = 2;
    const int min_after  = after.empty() ? 0 : 1;

    int max_before = threadBudget - min_par - min_after;
    if( !includes_parallel_scc(sccs,before) )
      max_before = std::min(max_before, (int)before.size() );

    int max_after = threadBudget - min_before - min_par;
    if( !includes_parallel_scc(sccs,after) )
      max_after = std::min(max_after, (int)after.size() );

    // Weight of the heaviest stage.  Smaller is better.
    unsigned long bestScore = ~0UL;
    unsigned bestNumThreads = threadBudget + 1;
    PipelineStrategy::Stages best;
    for(int nbefore=min_before; nbefore<=max_before; ++nbefore)
    {
      // Recur on sub-problems
      PipelineStrategy::Stages myStages;
      unsigned long score_before;
      unsigned numThreadsUsed_before;
      doallAndPipeline(pdg,sccs,before,perf,ctrlspec,myStages,
        score_before, numThreadsUsed_before, nbefore,includeReplicableStages, includeParallelStages);

      myStages.push_back( PipelineStage(PipelineStage::Parallel, pdg, sccs, maxParallel) );

      const unsigned size_before = myStages.size();
      for(int nafter=min_after; nafter<=max_after; ++nafter)
      {
        unsigned long score_after;
        unsigned numThreadsUsed_after;
        doallAndPipeline(pdg,sccs,after,perf,ctrlspec, myStages,
          score_after, numThreadsUsed_after, nafter,includeReplicableStages, includeParallelStages);

        const int max_par = threadBudget - numThreadsUsed_before - numThreadsUsed_after;

        for(int npar=max_par; npar>=min_par; --npar)
        {
          myStages[ size_before - 1 ].parallel_factor = npar;

          const unsigned numThreadsUsed = numThreadsUsed_before + npar + numThreadsUsed_after;

          PipelineStrategy::Stages expanded = myStages; // copy
          PipelineStrategy::expandReplicatedStages( expanded );
          const unsigned long score = perf.estimate_pipeline_weight( expanded );

          if( score < bestScore || (score==bestScore && numThreadsUsed < bestNumThreads) )
          {
            bestScore = score;
            bestNumThreads = numThreadsUsed;
            best = myStages;
          }
        }

        // remove the 'after' stages before next iteration
        myStages.erase( myStages.begin() + size_before, myStages.end() );
      }
    }

    // If we found a good one
    if( bestNumThreads <= threadBudget )
    {
      stages.insert( stages.end(),
        best.begin(), best.end() );

      score_out = bestScore;
      numThreads_out = bestNumThreads;
      return true;
    }
  }

  // At this point, we could not find a parallel stage
  // among the input set of SCCs.
  // Maybe we can find a replicable stage?

  if( includeReplicableStages )
  {
    // This is a straightforward extension of Easwaran
    // to support replicable stages (ask Thom what that means).
    //
    // Build a pipeline: {before} REPLICABLE {after}
    SCCs::SCCSet maxReplicable, notMaxReplicable;
    if( findMaxReplicableStage(pdg,sccs,all_sccs,perf,maxReplicable,notMaxReplicable) )
    {
      SCCs::SCCSet before, after;
      pivot(pdg,sccs, notMaxReplicable, maxReplicable, before, after);

      const int min_before = before.empty() ? 0 : 1;
      const int min_after  = after.empty() ? 0 : 1;

      int max_before = threadBudget - min_after;
      if( !includes_parallel_scc(sccs,before) )
        max_before = std::min(max_before, (int)before.size() );

      unsigned long bestScore = ~0UL;
      unsigned bestNumThreads = threadBudget+1;
      PipelineStrategy::Stages best;

      for(int nbefore=max_before; nbefore>=min_before; --nbefore)
      {
        PipelineStrategy::Stages myStages;
        unsigned long score_before;
        unsigned numThreadsUsed_before;
        doallAndPipeline(pdg,sccs,before,perf,ctrlspec,
          myStages,score_before, numThreadsUsed_before,
          nbefore,includeReplicableStages, includeParallelStages);

        myStages.push_back( PipelineStage(PipelineStage::Replicable, pdg, sccs, maxReplicable) );

        const unsigned size_before = myStages.size();

        int max_after = threadBudget - numThreadsUsed_before;
        if( !includes_parallel_scc(sccs,after) )
          max_after = std::min(max_after, (int)after.size());

        for(int nafter=max_after; nafter>=min_after; --nafter)
        {
          assert( numThreadsUsed_before + nafter <= threadBudget && "Did the math wrong");

          unsigned long score_after;
          unsigned numThreadsUsed_after;
          doallAndPipeline(pdg,sccs,after,perf,ctrlspec,
            myStages, score_after, numThreadsUsed_after,
            nafter,includeReplicableStages, includeParallelStages);

          const unsigned numThreadsUsed = numThreadsUsed_before + numThreadsUsed_after;

          PipelineStrategy::Stages expanded = myStages; // copy!
          PipelineStrategy::expandReplicatedStages( expanded );
          const unsigned long score = perf.estimate_pipeline_weight( expanded );

          if( score < bestScore || (score==bestScore && numThreadsUsed < bestNumThreads))
          {
            bestScore = score;
            bestNumThreads = numThreadsUsed;
            best = myStages;
          }

          // remove the 'after' stages before next iteration
          myStages.erase( myStages.begin() + size_before, myStages.end() );
        }
      }

      if( bestNumThreads <= threadBudget )
      {
        stages.insert( stages.end(),
          best.begin(), best.end() );

        score_out = bestScore;
        numThreads_out = bestNumThreads;
        return true;
      }
    }
  }

  // Easwaran's base case.
  return greedyDSWP(pdg,sccs,all_sccs,perf,ctrlspec,stages,score_out,numThreads_out,threadBudget);
}

bool Pipeline::greedyDSWP(
  // Inputs
  const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all_sccs,
  PerformanceEstimator &perf, ControlSpeculation &ctrlspec,
  // Outputs
  PipelineStrategy::Stages &stages, unsigned long &score_out, unsigned &numThreadsUsed_out,
  // Inputs
  unsigned threadBudget)
{

  // Find a balanced DSWP pipeline using Ottoni's greedy stage-packing heuristic.
  const unsigned num_stages = std::min(threadBudget, (unsigned)all_sccs.size());
  if( num_stages > 1 )
  {
    // (See phdthesis_ottoni, page 104)
    unsigned long total_weight = estimate_weight(pdg,sccs,perf, all_sccs);

    // This is a priority queue of SCCs, keyed by their in-degree.
    // We will repeatedly choose one SCC from this queue, remove it,
    // and decrease the key of its successor SCCs.
    unsigned N = all_sccs.size();
    typedef std::vector< SCCs::SCCSet > PrioQ;
    PrioQ prio;
    {
      // TODO this is ugly and possibly slow because our DAG_SCC
      // representation does not explicitly represent edges among
      // SCCs.
      // Beware the hidden complexity: each call to sccs.hasEdge(pdg,A,B)
      // checks for edges from any vertex in A to any other vertex in B.

      // First, compute the in-degree of each SCC
      typedef std::map<unsigned,unsigned> Scc2InDegree;

      Scc2InDegree in_degrees;
      for(unsigned i=0; i<N; ++i)
      {
        const unsigned A = all_sccs[i];
        in_degrees[A] = 0;

        for(unsigned j=0; j<N; ++j)
        {
          const unsigned B = all_sccs[j];

          if( A != B && sccs.hasEdge(pdg, sccs.get(B), sccs.get(A) ) )
            ++in_degrees[A];
        }
      }

      // Invert that into a priority queue.
      for(Scc2InDegree::const_iterator i=in_degrees.begin(), e=in_degrees.end(); i!=e; ++i)
      {
        const unsigned ind = i->second;
        if( ind + 1 > prio.size() )
          prio.resize( ind + 1 );

        prio[ ind ].push_back( i->first );
      }
    }

//    errs() << "(Looking for a " << num_stages << "-stage DSWP pipeline)...\n";
    unsigned long max_stage_weight = 0;
    unsigned stageno;
    for(stageno=0; stageno<num_stages && N>0; ++stageno)
    {
      const unsigned num_stages_remaining = num_stages - stageno;

      unsigned long target_weight = (total_weight + num_stages_remaining - 1) / num_stages_remaining;
      if( num_stages_remaining == 1 )
        target_weight = 2*total_weight;

//      errs() << "+ Stage " << stageno << " / " << num_stages << ": target weight " << target_weight << '\n';

      unsigned long stage_weight = 0;
      SCCs::SCCSet stage_sccs;

      // Pack this stage.
      while( stage_weight < target_weight  && N > 0 )
      {
//        errs() << "+ + Packing. " << stage_sccs.size() << " SCCs.  Weight " << stage_weight << '\n';

        const unsigned M = prio[0].size();

        // Choose one SCC from those SCCs which are ready (in prio[0])
        unsigned choice_scc = ~0U;
        unsigned choice_score = ~0U;
        unsigned choice_index = 0;
        EdgeWeight choice_weight = 0;

        assert( M > 0 );
        for(unsigned i=0; i<M; ++i)
        {
          unsigned candidate_scc = prio[0][i];
          EdgeWeight canidate_weight = estimate_weight(pdg,sccs,perf,candidate_scc);

          // Special case: pack zero-weight SCCs as early as possible.
          if( canidate_weight == 0 )
          {
//            errs() << "+ + + Best SCC #" << candidate_scc << " has weight 0.\n";

            choice_scc = candidate_scc;
            choice_weight = canidate_weight;
            choice_index = i;
            break;
          }

          // Score is a measure of how close the stage is
          // to the desired weight == abs( target - candidate ).
          unsigned candidate_stage_weight = stage_weight + canidate_weight;
          unsigned candidate_score = (candidate_stage_weight > target_weight) ? (candidate_stage_weight - target_weight) : (target_weight - candidate_stage_weight);
          if( candidate_score < choice_score )
          {
 //           errs() << "+ + + Best SCC #" << candidate_scc << " has weight " << canidate_weight << " and score " << candidate_score << ".\n";
            choice_scc = candidate_scc;
            choice_weight = canidate_weight;
            choice_score = candidate_score;
            choice_index = i;
          }
          else
          {
//            errs() << "+ + + SCC #" << candidate_scc << " has weight " << canidate_weight << " and score " << candidate_score << ".\n";
          }
        }

        // Add that choice to this stage...
        assert( choice_scc != ~0U );
        stage_sccs.push_back(choice_scc);
        stage_weight += choice_weight;

        // Remove it from the priority queue
        prio[0][ choice_index ] = prio[0].back();
        prio[0].pop_back();
        --N;

        // ...and decrease the key of its successors.
        for(unsigned i=1; i<prio.size(); ++i)
          for(unsigned j=0; j<prio[i].size(); ++j)
          {
            unsigned other_scc = prio[i][j];
            if( sccs.hasEdge(pdg, sccs.get(choice_scc), sccs.get(other_scc) ) )
            {
//              errs() << "+ + + + Decrease key for successor SCC #" << other_scc << " from " << i << " to " << (i-1) << ".\n";
              // Remove 'other_scc' from prio[i],
              prio[i][j] = prio[i].back();
              prio[i].pop_back();

              // Add 'other_scc' to prio[i-1]
              prio[i-1].push_back( other_scc );

              // Restart the loop.
              --j;
            }
          }
      }

      PipelineStage seq( PipelineStage::Sequential, pdg, sccs, stage_sccs );
      stages.push_back(seq);

      if( stage_weight > max_stage_weight )
        max_stage_weight = stage_weight;

      total_weight -= stage_weight;
    }

    score_out = max_stage_weight;
    numThreadsUsed_out = stageno;
    return true;
  }

  // Build a singleton pipline: SEQUENTIAL
  PipelineStage seq = PipelineStage( PipelineStage::Sequential, pdg, sccs, all_sccs);
  stages.push_back(seq);

  score_out = estimate_weight(pdg,sccs,perf,seq);
  numThreadsUsed_out = 1;
  return true;
}

struct IsParallel
{
  IsParallel(const SCCs &S) : sccs(S) {}

  bool operator()(const SCCs::SCC &scc) const
  {
    return !sccs.mustBeInSequentialStage(scc);
  }

private:
  const SCCs &sccs;
};

struct LoopCarriedBetweenSCCs
{
  LoopCarriedBetweenSCCs(const PDG &p) : pdg(p) {}

  bool operator()(const SCCs::SCC &scc1, const SCCs::SCC &scc2) const
  {
    for(unsigned i=0, N=scc1.size(); i<N; ++i)
    {
      Vertices::ID v1 = scc1[i];

      for(unsigned j=0, M=scc2.size(); j<M; ++j)
      {
        Vertices::ID v2 = scc2[j];

        if( pdg.hasLoopCarriedEdge(v1,v2) )
          return true;
      }
    }
    return false;
  }

private:
  const PDG &pdg;
};

struct IsReplicable
{
  IsReplicable(const Vertices &V) : vertices(V) {}

  bool operator()(const SCCs::SCC &scc) const
  {
    for(SCCs::SCC::const_iterator i=scc.begin(), e=scc.end(); i!=e; ++i)
    {
      const Instruction *inst = vertices.get(*i);
      if( inst->mayWriteToMemory() )
        return false;
      if( FengHack && inst->mayReadFromMemory() )
        return false;
    }

    return true;
  }

private:
  const Vertices &vertices;
};

struct IsLightweight
{
  IsLightweight(const Vertices &V, ControlSpeculation &CS)
    : vertices(V), ctrlspec(CS) {}

  bool operator()(const SCCs::SCC &scc) const
  {
    for(SCCs::SCC::const_iterator i=scc.begin(), e=scc.end(); i!=e; ++i)
    {
      const Instruction *inst = vertices.get(*i);
      if( isa<PHINode>(inst) )
        continue;
      if( isa<BranchInst>(inst) )
        continue;
      if( isa<SwitchInst>(inst) )
        continue;
      if( ctrlspec.isSpeculativelyDead(inst) )
        continue;

      return false;
    }

    return true;
  }

private:
  const Vertices &vertices;
  ControlSpeculation &ctrlspec;
};

struct IsParallelAndNotBetterInReplicable
{
  IsParallelAndNotBetterInReplicable(const SCCs &S, const Vertices &V, ControlSpeculation &CS)
    : parallel(S), replicable(V), lightweight(V,CS) {}

  bool operator()(const SCCs::SCC &scc) const
  {
    if( !parallel(scc) )
      return false; // not parallel

    if( !replicable(scc) )
      return true; // parallel but not replicable

    if( lightweight(scc) )
      return false; // parallel and replicable and lightweight

    // parallel and replicable and not lightweight
    return true;
  }
private:
  IsParallel parallel;
  IsReplicable replicable;
  IsLightweight lightweight;
};


struct AllSCCsAreCompatible
{
  bool operator()(const SCCs::SCC &a, const SCCs::SCC &b) const { return false; }
};

bool Pipeline::findMaxParallelStage(
  const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all_sccs,
  PerformanceEstimator &perf, ControlSpeculation &ctrlspec,
  bool includeReplicableStages,
  SCCs::SCCSet &maxPar, SCCs::SCCSet &notMaxPar)
{
  IsParallel pred(sccs);
  LoopCarriedBetweenSCCs incompat(pdg);

  return findMaxGoodStage< IsParallel, LoopCarriedBetweenSCCs >(
    pdg,sccs,all_sccs,perf, pred,incompat, maxPar,notMaxPar);
}

bool Pipeline::findMaxReplicableStage(
  const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all_sccs,
  PerformanceEstimator &perf,
  SCCs::SCCSet &maxRep, SCCs::SCCSet &notMaxRep)
{
  IsReplicable pred(pdg.getV());
  AllSCCsAreCompatible rel;
  if( !findMaxGoodStage< IsReplicable, AllSCCsAreCompatible >(pdg,sccs,all_sccs,perf,pred,rel,maxRep,notMaxRep) )
    return false;

/* Two problems with the following code.
 * (1) Rather than checking for an immediate ordering, it should be
 *     checking if SCCs are transitively ordered after
 *     an SCC in a non-replicable stage.
 * (2) This assumes that all replicable
 *     stages have already been identified, thus breaking the divide-and-conquer
 *     recurrence pattern; and,
 * I've disabled it, thus allowing replicable stages to consume. -NPJ

  // Reject any replicable stage which requires communication from
  // and earlier stage: first, map the set of SCC IDs into a set
  // of Vertex IDs.
  typedef std::set< Vertices::ID > VSet;
  VSet rep_stage;
  for(unsigned i=0, N=maxRep.size(); i<N; ++i)
  {
    const unsigned scc_id = maxRep[i];
    const SCCs::SCC &scc = sccs.get(scc_id);
    rep_stage.insert(
      scc.begin(), scc.end() );
  }
  // Check for any edge src->dst
  //  where
  //    src \not\in replicable stage, and
  //    dst \in replicable stage.
  for(Vertices::ID src=0, N=pdg.numVertices(); src<N; ++src)
  {
    if( rep_stage.count(src) )
      continue;

    for(VSet::const_iterator j=rep_stage.begin(), z=rep_stage.end(); j!=z; ++j)
    {
      Vertices::ID dst = *j;
      if( pdg.hasEdge(src,dst) )
      {
        // There is an edge; reject this replicable stage.
        return false;
      }
    }
  }
*/
  return true;
}

// Map an SCC ID to a Vertex ID in the NM-Flow graph
static Vertex Left(unsigned scc_id) { return 0+2*(scc_id+1); }
static Vertex Right(unsigned scc_id) { return 1+2*(scc_id+1); }

static void non_mergeable(unsigned scc1, unsigned scc2, Adjacencies &nonmergeable, EdgeWeights &nmweights)
{
  const Vertex left = Left( scc1 );
  const Vertex right = Right( scc2 );

  nonmergeable[ left ].push_back( right );
  nmweights[ Edge(left,right) ] = Infinity;
}

static void non_mergeable(const SCCs::SCCSet &A, const SCCs::SCCSet &B, Adjacencies &nonmergeable, EdgeWeights &nmweights)
{
  for(unsigned j=0; j<A.size(); ++j)
    for(unsigned k=0; k<B.size(); ++k)
      non_mergeable(A[j], B[k], nonmergeable, nmweights);
}

template< class Predicate, class Relation >
bool Pipeline::findMaxGoodStage(
  const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &all_sccs,
  PerformanceEstimator &perf,
  const Predicate &pred, const Relation &incompat, SCCs::SCCSet &good_sccs, SCCs::SCCSet &bad_sccs)
{
  // First, find the subset P \subseteq all_sccs of DOALL sccs.
  SCCs::SCCSet good, bad;
  for(SCCs::SCCSet::const_iterator i=all_sccs.begin(), e=all_sccs.end(); i!=e; ++i)
  {
    unsigned scc_id = *i;
    const SCCs::SCC &scc = sccs.get(scc_id);

    if( pred(scc) )
      good.push_back(scc_id);
    else
      bad.push_back(scc_id);
  }

  // No good SCCs?
  const unsigned N_good = good.size();
  if( N_good < 1 )
    return false;

  // Construct the non-mergeability flow network
  Adjacencies nonmergeable;
  EdgeWeights nmweights;

  for(unsigned i=0; i<N_good; ++i)
  {
    unsigned good_scc_id = good[i];
    const Vertex left = Left(good_scc_id);
    const Vertex right = Right(good_scc_id);

    const EdgeWeight profile_weight = estimate_weight(pdg,sccs,perf,good_scc_id);

    // Construct edge Source->left, with weight proportional to profile
    // weight.  Algorithm assumes non-zero weight.
    nonmergeable[Source].push_back( left );
    nmweights[ Edge(Source,left) ] = 1 + 100*profile_weight;

    // Construct edge right->Sink, with weight proportional to profile
    // weight.  Algorithm assumes non-zero weight.
    nonmergeable[right].push_back(Sink);
    nmweights[ Edge(right,Sink) ] = 1 + 100*profile_weight;
  }

  // RULE 1: Pipelines must be acyclic.
  // The 'good' stage cannot contain both
  // 'good' SCCs X,Y if there exists a path
  // X -> ... -> s -> ... -> Y, where s is a
  // 'bad' SCC.  This would create a cyclic
  // pipeline, since s is necessarily in a separate
  // 'bad' stage.  Prevent that here:
  for(unsigned i=0, N_bad=bad.size(); i<N_bad; ++i)
  {
    unsigned bad_scc_id = bad[i];
    SCCs::SCCSet pivots;
    pivots.push_back( bad_scc_id );

    // Find those SCCs from 'good' which must go before/after it.
    SCCs::SCCSet A,B,X;
    pivot(pdg,sccs, good, pivots, A,B,X);

    // Groups A,B are non-mergeable.
    non_mergeable(A,B, nonmergeable, nmweights);
  }

  // RULE 2: No loop carried deps within parallel stage.
  // The parallel stage cannot contain both
  // parallel SCCs X,Y if there exists a path
  // X -> ... -> Y which includes a loop-carried
  // dependence.  Prevent that here:

  // Foreach loop-carried dep from scc1 to scc2:
  for(unsigned i=0; i<N_good; ++i)
  {
    unsigned scc1 = good[i];
    const SCCs::SCC &first = sccs.get(scc1);

    for(unsigned j=0; j<N_good; ++j)
    {
      unsigned scc2 = good[j];
      const SCCs::SCC &second = sccs.get(scc2);

      // Is there a loop-carried dep from scc1 to scc2?
      if( incompat(first,second) )
      {
        // Let A = {scc1} U {all SCCs from 'good' which must precede scc1}
        SCCs::SCCSet pivots(1),A,B,dummy;
        pivots[0] = scc1;
        pivot(pdg,sccs, good, pivots, A,dummy,dummy);
        A.push_back(scc1);

        // Let B = {scc2} U {all SCCs from 'good' which must follow scc2}
        pivots[0] = scc2;
        pivot(pdg,sccs, good, pivots, dummy,B,dummy);
        B.push_back(scc2);

        // Groups A,B are non-mergeable.
        non_mergeable(A,B, nonmergeable, nmweights);
      }
    }
  }

  VertexSet minCut;
  computeMinCut(nonmergeable,nmweights, minCut);

  // The optimal parallel stage is the set of
  // SCCs from 'good' which are not in the min-cut.
  for(unsigned i=0; i<N_good; ++i)
  {
    unsigned good_scc = good[i];

    const Vertex lgood = Left(good_scc);
    const Vertex rgood = Right(good_scc);

    if( std::find(minCut.begin(),minCut.end(), lgood) == minCut.end()
    &&  std::find(minCut.begin(),minCut.end(), rgood) == minCut.end() )
      good_sccs.push_back( good_scc );
  }

  if( good_sccs.empty() )
    return false;

  for(SCCs::SCCSet::const_iterator i=all_sccs.begin(), e=all_sccs.end(); i!=e; ++i)
    if( std::find(good_sccs.begin(),good_sccs.end(), *i) == good_sccs.end() )
      bad_sccs.push_back(*i);

  return true;
}

bool Pipeline::isApplicable(Loop *loop, LoopAA *loopAA,
                            ControlSpeculation &ctrlspec,
                            PredictionSpeculation &predspec,
                            PerformanceEstimator &perf, unsigned threadBudget,
                            bool ignoreAntiOutput, bool includeReplicableStages,
                            bool constrainSubLoops, bool abortIfNoParallelStage,
                            bool includeParallelStages) {
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();
  errs() << "Pipeline::isApplicable(" << fcn->getName()
         << "::" << header->getName() << "):\n";

  PipelineStrategy strat;
  if (suggest(loop, loopAA, ctrlspec, predspec, perf, strat, threadBudget,
              ignoreAntiOutput, includeReplicableStages, constrainSubLoops,
              abortIfNoParallelStage, includeParallelStages))
    return true;

  return false;
}
}
}

