#define DEBUG_TYPE "ps-dswp-critic"
//#define AVOID_DSWP

#include "liberty/Orchestration/PSDSWPCritic.h"
#include "liberty/Utilities/WriteGraph.h"
#include "noelle/core/SCCDAG.hpp"
#include "scaf/SpeculationModules/TXIOAA.h"
#include "scaf/Utilities/ReportDump.h"

#include <unordered_set>
#include <climits>

#define OffPStagePercThreshold 3
#define OffPStageEdgeCostThreshold 100
#define FIXED_POINT 1000

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;

static EdgeWeight estimate_weight(PerformanceEstimator &perf, SCC *scc) {
  double sum_weight = 0.0;

  for (auto instPair : scc->internalNodePairs()) {
    Instruction *inst = dyn_cast<Instruction>(instPair.first);
    assert(inst);

    sum_weight += perf.estimate_weight(inst);
  }

  return FIXED_POINT * sum_weight;
}

/*
static EdgeWeight estimate_weight(PerformanceEstimator &perf,
                                  const PipelineStage &stage) {
  return perf.estimate_weight( stage.instructions.begin(),
stage.instructions.end() );
}

static EdgeWeight estimate_weight(PerformanceEstimator &perf,
                                  const SCCDAG::SCCSet &scc_set) {
  EdgeWeight sum_weight = 0;

  for (auto scc: scc_set) {
    sum_weight += estimate_weight(perf, scc);
  }

  return sum_weight;
}
*/

long getMinRemovalCost(DGEdge<Value> *edge) {
  if (!edge->isRemovableDependence())
    return LONG_MAX;
  auto sors = edge->getRemedies();
  auto cheapestR = *(sors->begin());
  return LoopAA::totalRemedCost(*cheapestR);
}

void pivot(const PDG &pdg, const SCCDAG &sccdag, const SCCDAG::SCCSet &all,
           const SCCDAG::SCCSet &pivots, SCCDAG::SCCSet &before,
           SCCDAG::SCCSet &after, SCCDAG::SCCSet &flexible) {
  for (auto *scc : all) {

    // Does scc come before any of the SCCs in maxParallel?
    if (sccdag.orderedBefore(scc, pivots))
      before.push_back(scc);

    else if (sccdag.orderedBefore(pivots, scc))
      after.push_back(scc);

    else
      flexible.push_back(scc);
  }
}

void pivot(const PDG &pdg, const SCCDAG &sccdag, const SCCDAG::SCCSet &all,
           const SCCDAG::SCCSet &pivots, SCCDAG::SCCSet &before,
           SCCDAG::SCCSet &after) {
  SCCDAG::SCCSet flexible;

  pivot(pdg, sccdag, all, pivots, before, after, flexible);

  // Favor shorter pipelines
  if (after.empty())
    before.insert(before.end(), flexible.begin(), flexible.end());
  else
    after.insert(after.end(), flexible.begin(), flexible.end());
}

struct IsParallel {
  IsParallel() {}

  bool operator()(const SCC &scc) const {
    for (auto edge : make_range(scc.begin_edges(), scc.end_edges())) {
      // all internal dependences must be intra-iteration
      if (!scc.isInternal(edge->getIncomingT()) ||
          !scc.isInternal(edge->getOutgoingT()))
        continue;

      if (edge->isLoopCarriedDependence()) {

        // REPORT_DUMP(errs() << "loop-carried edge(s) found from "
        //             << *edge->getOutgoingT() << " to " <<
        //             *edge->getIncomingT()
        //             << '\n');

        return false;
      }
    }
    // return scc.getType() == SCC::SCCType::INDEPENDENT;
    return true;
  }
};

/*
// see if loop carried deps between SCCs can be removed with prematerialization
bool checkIfRemateriazable(const DGEdge<SCC> *edge, const SCC &outgoingSCC) {
  for (auto subEdge : make_range(edge->begin_sub_edges(), edge->end_sub_edges())) {
    if (!subEdge->isRAWDependence() || subEdge->isMemoryDependence()) {
      return false;
    }
  }
  // this is remateriazable dependence.
  // TODO: note down this edge and apply rematerialization in the
  // application of PS-DSWP
  // need also to check whether it is speculative or not.
  // determines whether rematerialization will be part of transaction
  REPORT_DUMP(errs() << "Remateriazable variable(s) found\n");
  return true;
}
*/

// fetchNode should have a const version to avoid casting
struct LoopCarriedBetweenSCCs {
  LoopCarriedBetweenSCCs(const SCCDAG &sd)
      : sccdag(*const_cast<SCCDAG *>(&sd)) {}

  bool operator()(const SCC &scc1, const SCC &scc2) const {
    auto sccNode1 = sccdag.fetchNode(const_cast<SCC *>(&scc1));
    for (auto edge : sccNode1->getIncomingEdges()) {
      if (edge->getOutgoingNode()->getT() == &scc2 &&
          edge->isLoopCarriedDependence()) {
        //if (checkIfRemateriazable(edge, scc2))
        //  continue;
        return true;
      }
    }
    for (auto edge : sccNode1->getOutgoingEdges()) {
      if (edge->getIncomingNode()->getT() == &scc2 &&
          edge->isLoopCarriedDependence()) {
        //if (checkIfRemateriazable(edge, scc1))
        //  continue;
        return true;
      }
    }

    return false;
  }

private:
  SCCDAG &sccdag;
};

struct IsReplicable {
  IsReplicable() {}

  bool operator()(const SCC &scc) const {
    // need to create more const iterators in noelle
    for (auto instPair : const_cast<SCC *>(&scc)->internalNodePairs()) {
      if (!scc.isInternal(instPair.first))
        continue;
      const Instruction *inst = dyn_cast<Instruction>(instPair.first);
      assert(inst);

      if (inst->mayWriteToMemory())
        return false;
    }

    return true;
  }
};

struct IsLightweight {
  IsLightweight() {}

  bool operator()(const SCC &scc) const {
    for (auto instPair : const_cast<SCC *>(&scc)->internalNodePairs()) {
      if (!scc.isInternal(instPair.first))
        continue;
      const Instruction *inst = dyn_cast<Instruction>(instPair.first);
      assert(inst);

      if (isa<PHINode>(inst))
        continue;
      if (isa<BranchInst>(inst))
        continue;
      if (isa<SwitchInst>(inst))
        continue;

      return false;
    }

    return true;
  }
};

struct IsParallelAndNotBetterInReplicable {
  IsParallelAndNotBetterInReplicable()
      : parallel(), replicable(), lightweight() {}

  bool operator()(const SCC &scc) const {
    if (!parallel(scc))
      return false; // not parallel

    if (!replicable(scc))
      return true; // parallel but not replicable

    if (lightweight(scc))
      return false; // parallel and replicable and lightweight

    // parallel and replicable and not lightweight
    return true;
  }

private:
  IsParallel parallel;
  IsReplicable replicable;
  IsLightweight lightweight;
};

struct AllSCCsAreCompatible {
  bool operator()(const SCC &a, const SCC &b) const { return false; }
};

// Map an SCC ID to a Vertex ID in the NM-Flow graph
static Vertex Left(unsigned scc_id) { return 0 + 2 * (scc_id + 1); }
static Vertex Right(unsigned scc_id) { return 1 + 2 * (scc_id + 1); }

static void non_mergeable(unsigned scc1, unsigned scc2,
                          Adjacencies &nonmergeable, EdgeWeights &nmweights) {
  const Vertex left = Left(scc1);
  const Vertex right = Right(scc2);

  nonmergeable[left].push_back(right);
  nmweights[Edge(left, right)] = Infinity;
}

bool isReplicable(Instruction *inst) {
  if (inst->mayWriteToMemory())
    return false;

  return true;
}

static void non_mergeable(const SCCDAG &sccdag, const SCCDAG::SCCSet &A,
                          const SCCDAG::SCCSet &B, Adjacencies &nonmergeable,
                          EdgeWeights &nmweights) {
  for (auto sccA : A)
    for (auto sccB : B)
      non_mergeable(sccdag.getSCCIndex(sccA), sccdag.getSCCIndex(sccB),
                    nonmergeable, nmweights);
}

template <class Predicate, class Relation>
bool findMaxGoodStage(const PDG &pdg, const SCCDAG &sccdag,
                      const SCCDAG::SCCSet &all_sccs,
                      PerformanceEstimator &perf, const Predicate &pred,
                      const Relation &incompat, SCCDAG::SCCSet &good_sccs,
                      SCCDAG::SCCSet &bad_sccs) {
  // First, find the subset P \subseteq all_sccs of DOALL sccs.
  SCCDAG::SCCSet good, bad;
  for (auto scc : all_sccs) {
    if (pred(*scc))
      good.push_back(scc);
    else
      bad.push_back(scc);
  }

  // No good SCCs?
  const unsigned N_good = good.size();
  if (N_good < 1)
    return false;

  // Construct the non-mergeability flow network
  Adjacencies nonmergeable;
  EdgeWeights nmweights;

  for (auto good_scc : good) {
    unsigned good_scc_id = sccdag.getSCCIndex(good_scc);
    const Vertex left = Left(good_scc_id);
    const Vertex right = Right(good_scc_id);

    const EdgeWeight profile_weight = estimate_weight(perf, good_scc);

    // Construct edge Source->left, with weight proportional to profile
    // weight.  Algorithm assumes non-zero weight.
    nonmergeable[Source].push_back(left);
    nmweights[Edge(Source, left)] = 1 + 100 * profile_weight;

    // Construct edge right->Sink, with weight proportional to profile
    // weight.  Algorithm assumes non-zero weight.
    nonmergeable[right].push_back(Sink);
    nmweights[Edge(right, Sink)] = 1 + 100 * profile_weight;
  }

  // RULE 1: Pipelines must be acyclic.
  // The 'good' stage cannot contain both
  // 'good' SCCs X,Y if there exists a path
  // X -> ... -> s -> ... -> Y, where s is a
  // 'bad' SCC.  This would create a cyclic
  // pipeline, since s is necessarily in a separate
  // 'bad' stage.  Prevent that here:
  for (auto bad_scc : bad) {
    // unsigned bad_scc_id = sccdag.getSCCIndex(bad_scc);
    SCCDAG::SCCSet pivots;
    pivots.push_back(bad_scc);

    // Find those SCCs from 'good' which must go before/after it.
    SCCDAG::SCCSet A, B, X;
    pivot(pdg, sccdag, good, pivots, A, B, X);

    // Groups A,B are non-mergeable.
    non_mergeable(sccdag, A, B, nonmergeable, nmweights);
  }

  // RULE 2: No loop carried deps within parallel stage.
  // The parallel stage cannot contain both
  // parallel SCCs X,Y if there exists a path
  // X -> ... -> Y which includes a loop-carried
  // dependence.  Prevent that here:

  // Foreach loop-carried dep from scc1 to scc2:
  for (auto scc1 : good) {

    for (auto scc2 : good) {
      // Is there a loop-carried dep from scc1 to scc2?
      if (incompat(*scc1, *scc2)) {
        // Let A = {scc1} U {all SCCs from 'good' which must precede scc1}
        SCCDAG::SCCSet pivots(1), A, B, dummy;
        pivots[0] = scc1;
        pivot(pdg, sccdag, good, pivots, A, dummy, dummy);
        A.push_back(scc1);

        // Let B = {scc2} U {all SCCs from 'good' which must follow scc2}
        pivots[0] = scc2;
        pivot(pdg, sccdag, good, pivots, dummy, B, dummy);
        B.push_back(scc2);

        // Groups A,B are non-mergeable.
        non_mergeable(sccdag, A, B, nonmergeable, nmweights);
      }
    }
  }

  VertexSet minCut;
  computeMinCut(nonmergeable, nmweights, minCut);

  // The optimal parallel stage is the set of
  // SCCs from 'good' which are not in the min-cut.
  for (auto good_scc : good) {
    unsigned good_scc_id = sccdag.getSCCIndex(good_scc);

    const Vertex lgood = Left(good_scc_id);
    const Vertex rgood = Right(good_scc_id);

    if (std::find(minCut.begin(), minCut.end(), lgood) == minCut.end() &&
        std::find(minCut.begin(), minCut.end(), rgood) == minCut.end())
      good_sccs.push_back(good_scc);
  }

  if (good_sccs.empty())
    return false;

  for (auto scc : all_sccs)
    if (std::find(good_sccs.begin(), good_sccs.end(), scc) == good_sccs.end())
      bad_sccs.push_back(scc);

  return true;
}

bool findMaxParallelStage(const PDG &pdg, const SCCDAG &sccdag,
                          const SCCDAG::SCCSet &all_sccs,
                          PerformanceEstimator &perf,
                          bool includeReplicableStages, SCCDAG::SCCSet &maxPar,
                          SCCDAG::SCCSet &notMaxPar) {
  IsParallel pred;
  LoopCarriedBetweenSCCs incompat(sccdag);

  return findMaxGoodStage<IsParallel, LoopCarriedBetweenSCCs>(
      pdg, sccdag, all_sccs, perf, pred, incompat, maxPar, notMaxPar);
}

bool findMaxReplicableStage(const PDG &pdg, const SCCDAG &sccdag,
                            const SCCDAG::SCCSet &all_sccs,
                            PerformanceEstimator &perf, SCCDAG::SCCSet &maxRep,
                            SCCDAG::SCCSet &notMaxRep) {
  IsReplicable pred;
  AllSCCsAreCompatible rel;
  if (!findMaxGoodStage<IsReplicable, AllSCCsAreCompatible>(
          pdg, sccdag, all_sccs, perf, pred, rel, maxRep, notMaxRep))
    return false;

  /* Two problems with the following code.
   * (1) Rather than checking for an immediate ordering, it should be
   *     checking if SCCs are transitively ordered after
   *     an SCC in a non-replicable stage.
   * (2) This assumes that all replicable
   *     stages have already been identified, thus breaking the
   divide-and-conquer
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

      for(VSet::const_iterator j=rep_stage.begin(), z=rep_stage.end(); j!=z;
   ++j)
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

bool PSDSWPCritic::doallAndPipeline(const PDG &pdg, const SCCDAG &sccdag,
                                    SCCDAG::SCCSet &all_sccs,
                                    PerformanceEstimator &perf,
                                    PipelineStrategy::Stages &stages,
                                    unsigned threadBudget,
                                    bool includeReplicableStages,
                                    bool includeParallelStages) {
  if (all_sccs.empty()) {
    return false;
  }

  assert(threadBudget > 0 && "Can't schedule >0 SCCs with <1 threads");

  // Build a pipline: {before} DOALL {after}
  SCCDAG::SCCSet maxParallel, notMaxParallel;
  if (includeParallelStages &&
      findMaxParallelStage(pdg, sccdag, all_sccs, perf, includeReplicableStages,
                           maxParallel, notMaxParallel)) {
    SCCDAG::SCCSet before, after;
    pivot(pdg, sccdag, notMaxParallel, maxParallel, before, after);

    // EdgeWeight total_weight = estimate_weight(perf, all_sccs);
    unsigned threadAvail = threadBudget;

    if (!before.empty()) {
      /*
      EdgeWeight seq_weight = estimate_weight(perf, before);

      // check if the sequential part is less than 10% of total loop weight
      if ((seq_weight * 100.0) / total_weight >= 10.0) {
        REPORT_DUMP(errs()
              << "PS-DSWP not applicable to " << fcn->getName()
              << "::" << header->getName()
              << "\nThe first sequential part is not less than 10% of total "
                 "loop weight\n");
        return false;
      }
      */

      stages.push_back(PipelineStage(PipelineStage::Sequential, pdg, before));

      --threadAvail;

      /*
      // At this point, we could not find a single parallel stage containing all
      // the sccs. Current pipeline: {before} parallel. Check if before is a
      // replicable stage

      SCCDAG::SCCSet maxReplicable, notMaxReplicable;

      //if (includeReplicableStages &&
      if (findMaxReplicableStage(pdg, sccdag, before, perf, maxReplicable,
                                 notMaxReplicable)) {
        if (!notMaxReplicable.empty()) {
          REPORT_DUMP(errs() << "DOALL not applicable to " << fcn->getName()
                       << "::" << header->getName()
                       << "\nNot all SCCs of the first sequential stage are "
                          "replicable\n");
          return false;
        }

        stages.push_back(
            PipelineStage(PipelineStage::Sequential, pdg, maxReplicable));
        // PipelineStage(PipelineStage::Replicable, pdg, sccdag,
        // maxReplicable));

        EdgeWeight seq_weight = estimate_weight(perf, maxReplicable);
        EdgeWeight total_weight = estimate_weight(perf, all_sccs);

        // check if the sequential part is less than 3% of total loop weight
        if ((seq_weight * 100.0) / total_weight >= 3.0) {
          REPORT_DUMP(errs() << "DOALL not applicable to " << fcn->getName()
                       << "::" << header->getName()
                       << "\nThe sequential part is not less than 3% of total "
                          "loop weight\n");
          return false;
        }

      } else {
        REPORT_DUMP(
            errs()
            << "DOALL not applicable to " << fcn->getName()
            << "::" << header->getName()
            << "\nCould not find replicable stage before parallel stage\n");
        return false;
      }
      */
    }

    if (!after.empty())
      --threadAvail;

    PipelineStage parallel_stage =
        PipelineStage(PipelineStage::Parallel, pdg, maxParallel);

    parallel_stage.parallel_factor = threadAvail;

    stages.push_back(parallel_stage);

    if (!after.empty()) {

      /*
      EdgeWeight seq_weight = estimate_weight(perf, after);

      // check if the sequential part is less than 10% of total loop weight
      if ((seq_weight * 100.0) / total_weight >= 10.0) {
        REPORT_DUMP(errs()
              << "PS-DSWP not applicable to " << fcn->getName()
              << "::" << header->getName()
              << "\nThe last sequential part is not less than 10% of total "
                 "loop weight\n");
        return false;
      }
      */

      stages.push_back(PipelineStage(PipelineStage::Sequential, pdg, after));
    }

    return true;
  }

  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();
  REPORT_DUMP(errs() << "PS-DSWP not applicable to " << fcn->getName() << "::"
               << header->getName() << "\nCould not find a parallel stage\n");

  return false;
}

bool PSDSWPCritic::doallAndPipeline(const PDG &pdg, const SCCDAG &sccdag,
                                          PerformanceEstimator &perf,
                                          PipelineStrategy::Stages &stages,
                                          unsigned threadBudget,
                                          bool includeReplicableStages,
                                          bool includeParallelStages) {
  // Set of all SCCs
  SCCDAG::SCCSet all_sccs;
  for (auto scc : make_range(sccdag.begin_nodes(), sccdag.end_nodes()))
    all_sccs.push_back(scc->getT());

  const bool res =
      doallAndPipeline(pdg, sccdag, all_sccs, perf, stages, threadBudget,
                       includeReplicableStages, includeParallelStages);

  /*
  if( stages.size() == 1
  &&  stages[0].type != PipelineStage::Parallel )
    return false;
  */

  return res;
}

void PSDSWPCritic::simplifyPDG(PDG *pdg) {
  std::vector<Value *> loopInternals;
  for (auto internalNode : pdg->internalNodePairs()) {
    loopInternals.push_back(internalNode.first);
  }
  optimisticPDG = pdg->createSubgraphFromValues(loopInternals, false);
  
  // use the sccdag manager from NOELLE


  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  unsigned long lcDepTotal = 0;
  unsigned long lcDepNotCovered = 0;
  unsigned long lcRAWMemDepTotal = 0;
  unsigned long lcWAWMemDepTotal = 0;
  unsigned long lcWARMemDepTotal = 0;
  unsigned long lcRegDepTotal = 0;
  unsigned long lcCtrlDepTotal = 0;

  // remove all the removable edges and produce optimistic pdg
  std::vector<DGEdge<Value> *> toBeRemovedEdges;
  for (auto edge : optimisticPDG->getEdges()) {
    if (edge->isLoopCarriedDependence()) {
      ++lcDepTotal;

      if (edge->isControlDependence())
        ++lcCtrlDepTotal;
      else if (edge->isMemoryDependence()) {
        if (edge->isRAWDependence())
          ++lcRAWMemDepTotal;
        else if (edge->isWAWDependence())
          ++lcWAWMemDepTotal;
        else if (edge->isWARDependence())
          ++lcWARMemDepTotal;
      }
      else
        ++lcRegDepTotal;

      if (!edge->isRemovableDependence()) {
        ++lcDepNotCovered;
      }
    }

    if (edge->isRemovableDependence())
      toBeRemovedEdges.push_back(edge);
  }

  for (auto edge : toBeRemovedEdges) {
    // all pdg nodes involved in a reduction should remain in the same scc (and
    // stage). Loop-carried deps handled by reduction cannot be removed
    // completely since the reduction cycle will be broken. These cycles should
    // remain but they can belong in a parallel stage. Thus, loop-carried deps
    // handled by reduction are marked as intra-iteration to allow integration
    // in parallel stage and avoid cross-stage distribution.
    if (getMinRemovalCost(edge) == DEFAULT_REDUX_REMED_COST &&
        edge->isLoopCarriedDependence()) {
      edge->setLoopCarried(false);
    } else {
      optimisticPDG->removeEdge(edge);
    }
  }

  REPORT_DUMP(errs() << "\nRAW Memory Loop-Carried Deps Count: " << lcRAWMemDepTotal
               << "\nWAW Memory Loop-Carried Deps Count: " << lcWAWMemDepTotal
               << "\nWAR Memory Loop-Carried Deps Count: " << lcWARMemDepTotal
               << "\nRegister Loop-Carried Deps Count: " << lcRegDepTotal
               << "\nControl Loop-Carried Deps Count: " << lcCtrlDepTotal
               << "\n");

  unsigned long lcDepCovered = lcDepTotal - lcDepNotCovered;
  double percentageCovered = (100.0 * lcDepCovered) / lcDepTotal;
  REPORT_DUMP(errs() << "\nCoverage of loop-carried dependences for hot loop "
               << fcn->getName() << " :: " << header->getName() << " "
               << "covered=" << lcDepCovered << ", total=" << lcDepTotal
               << " , percentage=" << format("%.2f", percentageCovered)
               << "%\n\n");

  std::string pdgDotName = "optimistic_pdg_" + header->getName().str() + "_" +
                           fcn->getName().str() + ".dot";

  writeGraph<PDG, Value>(pdgDotName, optimisticPDG);

  optimisticSCCDAG = new SCCDAG(optimisticPDG);

  std::string sccdagDotName = "optimistic_sccdag_" + header->getName().str() +
                              "_" + fcn->getName().str() + ".dot";

  writeGraph<SCCDAG, SCC>(sccdagDotName, optimisticSCCDAG);
}

// move inst and backward (if moveToFront is true) or forward slice of inst to
// the tgt seq stage from other seq or pstage
unsigned long PSDSWPCritic::moveOffStage(
    const PDG &pdg, Instruction *inst, unordered_set<Instruction *> &visited,
    set<Instruction *> *instsTgtSeq,
    unordered_set<Instruction *> &instsMovedTgtSeq,
    unordered_set<Instruction *> &instsMovedOtherSeq,
    set<Instruction *> *instsOtherSeq,
    unordered_set<DGEdge<Value> *> &edgesNotRemoved,
    const EdgeWeight curOffPStageWeight, bool moveToFront) {

  // percentage of weight moved off the parallel stage
  EdgeWeight extraOffPStageWeight = 0;

  unordered_set<Instruction *> &notMovableInsts =
      (moveToFront) ? notMovableInstsToFront : notMovableInstsToBack;

  // already moved to other seq stage, cannot be moved elsewhere
  if (instsMovedOtherSeq.count(inst)) {
    notMovableInsts.insert(inst);
    return ULONG_MAX;
  }

  if (instsMovedTgtSeq.count(inst))
    return 0;

  if (instsTgtSeq && instsTgtSeq->count(inst))
    return 0;

  if (visited.count(inst))
    return 0;
  visited.insert(inst);

  // check if the part moved to seq stage is more than threshold% of
  // parallel stage weight. Ignore if not reducing pstage
  if (!instsOtherSeq ||
      !instsOtherSeq->count(inst)) { // ignore cost moving from seq to seq
    extraOffPStageWeight += FIXED_POINT * perf->estimate_weight(inst);
    if (((extraOffPStageWeight + curOffPStageWeight) * 100.0) /
            parallelStageWeight >
        OffPStagePercThreshold) {
      notMovableInsts.insert(inst);
      return ULONG_MAX;
    }
  }

  auto pdgNode = pdg.fetchConstNode(const_cast<Instruction *>(inst));

  // avoid creating extra uncommitted mem value forwarding.
  // stores moved to first seq stage could create a lot of extra
  // communication for II or LC mem flows to later stages
  // This comm occurs at every single iteration; thus it needs to be avoided
  if (moveToFront && isa<StoreInst>(inst)) {
    for (auto edge : make_range(pdgNode->begin_outgoing_edges(),
                                pdgNode->end_outgoing_edges())) {

      Instruction *dstI = dyn_cast<Instruction>(edge->getIncomingT());
      if (!dstI)
        continue;

      if (edge->isMemoryDependence() && edge->isRAWDependence() &&
          // dstI is not in first seq stage
          !(instsMovedTgtSeq.count(dstI) ||
            (instsTgtSeq && instsTgtSeq->count(dstI))) &&
          // and exclude case where dstI is in last seq and store is in pstage
          !((instsMovedOtherSeq.count(dstI) ||
             (instsOtherSeq && instsOtherSeq->count(dstI))) &&
            !(instsOtherSeq && instsOtherSeq->count(inst)))) {

        // there is a mem flow from this store to a later stage. avoid moving it
        // exclude scenario where inst is in parallel stage and dstI is in last
        // sequential stage. In this scenario, it is more profitable to move
        // store to first stage and avoid criticism and move comm out of the
        // parallel stage
        notMovableInsts.insert(inst);
        return ULONG_MAX;
      }
    }
  }

  auto edges = (moveToFront) ? make_range(pdgNode->begin_incoming_edges(),
                                          pdgNode->end_incoming_edges())
                             : make_range(pdgNode->begin_outgoing_edges(),
                                          pdgNode->end_outgoing_edges());

  for (auto edge : edges) {

    // ignore cheap removable edges that are not redux
    if ((edge->isRemovableDependence() &&
         getMinRemovalCost(edge) < OffPStageEdgeCostThreshold &&
         getMinRemovalCost(edge) != DEFAULT_REDUX_REMED_COST) &&
        !edgesNotRemoved.count(edge))
      continue;

    Value *V = (moveToFront) ? edge->getOutgoingT() : edge->getIncomingT();
    if (!pdg.isInternal(V))
      continue;
    Instruction *I = dyn_cast<Instruction>(V);
    assert(I && "pdg node is not an instruction");

    EdgeWeight moveCost = moveOffStage(
        pdg, I, visited, instsTgtSeq, instsMovedTgtSeq, instsMovedOtherSeq,
        instsOtherSeq, edgesNotRemoved, curOffPStageWeight, moveToFront);

    if (moveCost == ULONG_MAX) {
      notMovableInsts.insert(inst);
      return ULONG_MAX;
    } else
      extraOffPStageWeight += moveCost;
  }
  return extraOffPStageWeight;
}

// consider avoid removing dep if it is cheap to move affected
// insts to different stages
bool PSDSWPCritic::avoidElimDep(
    const PDG &pdg, PipelineStrategy &ps, DGEdge<Value> *edge,
    unordered_set<Instruction *> &instsMovedToFront,
    unordered_set<Instruction *> &instsMovedToBack,
    unordered_set<DGEdge<Value> *> &edgesNotRemoved) {

  // check if we surpassed the threshold of offPStage movement already
  if ((offPStageWeight * 100.0) / parallelStageWeight > OffPStagePercThreshold)
    return false;

  // try to avoid lamp/slamp remedies, locality-private, mem ver and priv remed
  if (getMinRemovalCost(edge) < OffPStageEdgeCostThreshold)
    return false;

  Value *inV = edge->getIncomingT();
  Instruction *inI = dyn_cast<Instruction>(inV);
  Value *outV = edge->getOutgoingT();
  Instruction *outI = dyn_cast<Instruction>(outV);

  // either move the src dep inst to the first seq stage (if any) or the dest
  // dep inst to the last seq stage (if any).
  // similarly proceed with the rest of the dependent instructions until the
  // pstage is reduced below a threshold

  // if one of the inst is already moved to the appropriate stage, dep can be
  // avoided with no additional movement
  if (inI && outI && (instsMovedToFront.count(outI) || instsMovedToBack.count(inI)))
    return true;

  unsigned numOfStages = ps.stages.size();
  bool alreadyFrontSeqStage = ps.stages[0].type == PipelineStage::Sequential;
  bool alreadyBackSeqStage =
      numOfStages > 1 &&
      ps.stages[numOfStages - 1].type == PipelineStage::Sequential;
  set<Instruction *> *instsFrontSeqStage =
      (alreadyFrontSeqStage) ? &ps.stages[0].instructions : nullptr;
  set<Instruction *> *instsBackSeqStage =
      (alreadyBackSeqStage) ? &ps.stages[numOfStages - 1].instructions
                            : nullptr;

  unsigned long moveFrontCost = ULONG_MAX;
  unordered_set<Instruction *> tmpInstsMovedToFront;
  // TODO: probably requiring alreadyFrontSeqStage is not necessary
  if (pdg.isInternal(outV) && alreadyFrontSeqStage) {
    assert(outI && "pdg node is not an instruction");
    moveFrontCost =
        moveOffStage(pdg, outI, tmpInstsMovedToFront, instsFrontSeqStage,
                     instsMovedToFront, instsMovedToBack, instsBackSeqStage,
                     edgesNotRemoved, offPStageWeight, true);
  }

  unsigned long moveBackCost = ULONG_MAX;
  unordered_set<Instruction *> tmpInstsMovedToBack;
  if (pdg.isInternal(inV) && alreadyBackSeqStage) {
    assert(inI && "pdg node is not an instruction");
    moveBackCost =
        moveOffStage(pdg, inI, tmpInstsMovedToBack, instsBackSeqStage,
                     instsMovedToBack, instsMovedToFront, instsFrontSeqStage,
                     edgesNotRemoved, offPStageWeight, false);
  }

  if (moveFrontCost != ULONG_MAX && moveFrontCost <= moveBackCost) {
    edgesNotRemoved.insert(edge);
    offPStageWeight += moveFrontCost;
    REPORT_DUMP(errs() << "\nWill not remove edge(s) (removal cost: "
                 << getMinRemovalCost(edge) << " ) from "
                 << *edge->getOutgoingT() << " to " << *edge->getIncomingT()
                 << '\n');
    for (Instruction *I : tmpInstsMovedToFront) {
      instsMovedToFront.insert(I);
      REPORT_DUMP(errs() << "Move inst to first sequential stage: " << *I << '\n');
    }
    return true;
  } else if (moveBackCost != ULONG_MAX && moveBackCost < moveFrontCost) {
    edgesNotRemoved.insert(edge);
    offPStageWeight += moveBackCost;
    REPORT_DUMP(errs() << "\nWill not remove edge(s) (removal cost: "
                 << getMinRemovalCost(edge) << " ) from "
                 << *edge->getOutgoingT() << " to " << *edge->getIncomingT()
                 << '\n');
    for (Instruction *I : tmpInstsMovedToBack) {
      instsMovedToBack.insert(I);
      REPORT_DUMP(errs() << "Move inst to last sequential stage: " << *I << '\n');
    }
    return true;
  }
  return false;
}

// There should be no dependence from an instruction in 'later'
// to an instruction in 'earlier' stage
void PSDSWPCritic::critForPipelineProperty(const PDG &pdg,
                                           const PipelineStage &earlyStage,
                                           const PipelineStage &lateStage,
                                           Criticisms &criticisms,
                                           PipelineStrategy &ps) {

  PipelineStage::ISet all_early = earlyStage.instructions;
  all_early.insert(earlyStage.replicated.begin(), earlyStage.replicated.end());

  PipelineStage::ISet all_late = lateStage.instructions;
  all_late.insert(lateStage.replicated.begin(), lateStage.replicated.end());

  // For each operation from an earlier stage
  for (PipelineStage::ISet::const_iterator i = all_early.begin(),
                                           e = all_early.end();
       i != e; ++i) {
    Instruction *early = *i;
    auto *earlyNode = pdg.fetchConstNode(early);

    // For each operation from a later stage
    for (PipelineStage::ISet::const_iterator j = all_late.begin(),
                                             z = all_late.end();
         j != z; ++j) {
      Instruction *late = *j;
      auto *lateNode = pdg.fetchConstNode(late);

      // There should be no backwards dependence
      for (auto edge : make_range(lateNode->begin_outgoing_edges(),
                                  lateNode->end_outgoing_edges())) {
        if (edge->getIncomingNode() == earlyNode) {
          if (edge->isRemovableDependence()) {
              criticisms.insert(edge);
          } else {
            errs() << "\n\nNon-removable criticism found\n"
                   << "From: " << *late << '\n'
                   << "  to: " << *early << '\n'
                   << "From late stage:\n";
            lateStage.print_txt(errs());
            errs() << "To early stage:\n";
            earlyStage.print_txt(errs());

            assert(false && "Violated pipeline property");
          }
        }
      }
    }
  }
}

// There should be no loop-carried edges within 'parallel' stage
void PSDSWPCritic::critForParallelStageProperty(const PDG &pdg,
                                                const PipelineStage &parallel,
                                                Criticisms &criticisms,
                                                PipelineStrategy &ps) {

  PipelineStage::ISet all_insts = parallel.instructions;
  all_insts.insert(parallel.replicated.begin(), parallel.replicated.end());

  for (PipelineStage::ISet::const_iterator i = parallel.instructions.begin(),
                                           e = parallel.instructions.end();
       i != e; ++i) {
    Instruction *p = *i;
    auto *pN = pdg.fetchConstNode(p);

    for (PipelineStage::ISet::const_iterator j = all_insts.begin(),
                                             z = all_insts.end();
         j != z; ++j) {
      Instruction *q = *j;
      auto *qN = pdg.fetchConstNode(q);

      // There should be no loop-carried edge
      for (auto edge : make_range(pN->begin_outgoing_edges(), pN->end_outgoing_edges())) {
        if (edge->getIncomingNode() == qN && edge->isLoopCarriedDependence()) {
          if (edge->isRemovableDependence()) {
            criticisms.insert(edge);
          } else {
            errs() << "\n\nNon-removable criticism found\n"
                   << "From: " << *p << '\n'
                   << "  to: " << *q << '\n'
                   << "Loop-carried in parallel stage:\n";
            parallel.print_txt(errs());

            assert(false && "Violated parallel stage property");
          }
        }
      }
    }
  }
}

EdgeWeight PSDSWPCritic::getParalleStageWeight(PipelineStrategy &ps) {
  double parallelStageWeight = 0.0;
  for (unsigned i = 0, N = ps.stages.size(); i < N; ++i) {
    const PipelineStage &si = ps.stages[i];
    if (si.type == PipelineStage::Parallel) {
      for (PipelineStage::ISet::iterator j = si.instructions.begin(),
                                         z = si.instructions.end();
           j != z; ++j) {
        Instruction *I = *j;
        parallelStageWeight += perf->estimate_weight(I);
      }
    }
  }
  return FIXED_POINT * parallelStageWeight;
}

// try to remove expensive criticisms by moving instructions across stages
void PSDSWPCritic::avoidExpensiveCriticisms(const PDG &pdg,
                                            PipelineStrategy &ps,
                                            Criticisms &criticisms) {

  unordered_set<Instruction *> instsMovedToFront;
  unordered_set<Instruction *> instsMovedToBack;
  unordered_set<DGEdge<Value> *> edgesNotRemoved;

  for (auto edge : criticisms) {
    avoidElimDep(pdg, ps, edge, instsMovedToFront, instsMovedToBack,
                 edgesNotRemoved);
  }

  for (Instruction *I : instsMovedToFront) {
    ps.stages[0].instructions.insert(I);
    if (ps.stages[1].instructions.count(I))
      ps.stages[1].instructions.erase(I);
    else
      ps.stages[2].instructions.erase(I);
  }

  unsigned numOfStages = ps.stages.size();
  for (Instruction *I : instsMovedToBack) {
    ps.stages[numOfStages - 1].instructions.insert(I);
    if (ps.stages[numOfStages - 2].instructions.count(I))
      ps.stages[numOfStages - 2].instructions.erase(I);
    else
      ps.stages[numOfStages - 3].instructions.erase(I);
  }
}

void PSDSWPCritic::populateCrossStageDependences(PipelineStrategy &ps,
                                                 const Criticisms &criticisms,
                                                 PDG &pdg) {
  // Compute the set of control deps and mem flows which
  // span pipeline stages (and within the same stage kept for parallel stage OFF
  // iteration (replicated prefix))

  // Foreach stage
  for (unsigned i = 0, N = ps.stages.size(); i < N; ++i) {
    const PipelineStage &si = ps.stages[i];
    // Foreach instruction src in that stage that may source control deps
    PipelineStage::ISet all_insts = si.instructions;
    all_insts.insert(si.replicated.begin(), si.replicated.end());
    // for (PipelineStage::ISet::iterator j = si.instructions.begin(),
    //                                   z = si.instructions.end();
    for (PipelineStage::ISet::iterator j = all_insts.begin(),
                                       z = all_insts.end();
         j != z; ++j) {
      Instruction *src = *j;

      auto srcNode = pdg.fetchNode(src);
      for (auto edge : srcNode->getOutgoingEdges()) {
        Instruction *dst = dyn_cast<Instruction>(edge->getIncomingT());
        assert(dst &&
               "dst of ctrl dep is not an instruction in crossStageDeps");
        if (edge->isControlDependence() && src->isTerminator()) {
          // Foreach control-dep successor of src
          // also useful to keep ctrl deps within a stage for parallel stage's
          // OFF iteration
          // That spans stages.
          //        if( !si.instructions.count(dst) )

          // ignore ctrl deps that are removable and highly unlikely to
          // misspeculate
          if (!edge->isRemovableDependence() ||
              getMinRemovalCost(edge) == EXPENSIVE_CTRL_REMED_COST)
            ps.crossStageDeps.push_back(CrossStageDependence(src, dst, edge));

        } else if (edge->isMemoryDependence() && edge->isRAWDependence() &&
                   !criticisms.count(edge) && !all_insts.count(dst)) {
          // Foreach mem flow dep that spans stages and is forward (not part of
          // criticisms that contain all backward to the pipeline flows).
          ps.crossStageMemFlows.push_back(
              CrossStageDependence(src, dst, edge));
        }
      }
    }
  }
}

void PSDSWPCritic::characterizeStages(PipelineStrategy &ps,
                                      PipelineStage **firstStage,
                                      PipelineStage **parallelStage,
                                      PipelineStage **lastSeqStage) {
  for (PipelineStrategy::Stages::iterator i = ps.stages.begin(),
                                          e = ps.stages.end();
       i != e; ++i) {
    PipelineStage &pstage = *i;

    if (!*firstStage) {
      *firstStage = &pstage;
      if (pstage.type == PipelineStage::Parallel)
        *parallelStage = &pstage;
      continue;
    }

    if (pstage.type != PipelineStage::Parallel) {
      *lastSeqStage = &pstage;
      return;
    }
    *parallelStage = &pstage;
  }
}

void PSDSWPCritic::adjustForRegLCFromSeqToPar(PipelineStrategy &ps, PDG &pdg,
                                              PipelineStage *firstStage,
                                              PipelineStage *parallelStage) {
  std::vector<Instruction *> moveToSeqInsts;
  for (auto inst : parallelStage->instructions) {
     auto *pdgNode = pdg.fetchConstNode(inst);

    // There should be no loop-carried edge
    for (auto edge : make_range(pdgNode->begin_incoming_edges(),
                                pdgNode->end_incoming_edges())) {

      if (edge->isLoopCarriedDependence() && !edge->isControlDependence() &&
          !edge->isMemoryDependence() && edge->isRAWDependence() &&
          !edge->isRemovableDependence()) {
        moveToSeqInsts.push_back(inst);
      }
    }
  }

  // move selected insts from the parallel stage to the previous seq stage
  for (auto *inst : moveToSeqInsts) {
    parallelStage->instructions.erase(inst);
    firstStage->instructions.insert(inst);
    REPORT_DUMP(errs()
              << "Moved inst from parallel to previous sequential stage due to "
                 "a loop-carried, reg, RAW, non-removable dependence crossing "
                 "the two stages:\n    "
              << *inst << "\n";);
  }
}

void PSDSWPCritic::moveIOToLastSeqStage(PipelineStrategy &ps, PDG &pdg,
                                        PipelineStage *firstStage,
                                        PipelineStage *parallelStage,
                                        PipelineStage *lastSeqStage) {
  std::unordered_set<Instruction *> moveIOToLastSeq;
  bool movedAllIO = true;
  EdgeWeight tmpOffPStageWeight = offPStageWeight;
  std::vector<PipelineStage *> earlyStages;
  earlyStages.push_back(parallelStage);
  if (parallelStage != firstStage)
    earlyStages.push_back(firstStage);
  for (auto *st : earlyStages) {
    if (!st)
      continue;
    for (PipelineStage::ISet::const_iterator j = st->instructions.begin(),
                                             z = st->instructions.end();
         j != z; ++j) {
      Instruction *inst = *j;

      // check if IO deferral inst
      if (!TXIOAA::isTXIOFcn(inst))
        continue;

      if (!pdg.isInternal(inst))
        continue;

      set<Instruction *> *instsFrontSeqStage =
          (firstStage && firstStage->type != PipelineStage::Parallel)
              ? &firstStage->instructions
              : nullptr;
      set<Instruction *> *instsBackSeqStage = &lastSeqStage->instructions;

      std::unordered_set<Instruction *> tmpInstsMovedToBack;
      std::unordered_set<Instruction *> instsMovedToFront;
      std::unordered_set<DGEdge<Value> *> edgesNotRemoved;
      unsigned long moveBackCost =
          moveOffStage(pdg, inst, tmpInstsMovedToBack, instsBackSeqStage,
                       moveIOToLastSeq, instsMovedToFront, instsFrontSeqStage,
                       edgesNotRemoved, tmpOffPStageWeight, false);

      if (moveBackCost != ULONG_MAX) {
        tmpOffPStageWeight += moveBackCost;
        REPORT_DUMP(errs() << "Movable output I/O inst along with dependent insts to "
                        "last sequential stage, "
                     << *inst << '\n');
        for (auto *inst : tmpInstsMovedToBack) {
          moveIOToLastSeq.insert(inst);
        }
      } else {
        REPORT_DUMP(errs()
              << "Not movable output I/O inst along with dependent insts to "
                 "last sequential stage, "
              << *inst << '\n');
        for (auto *inst : tmpInstsMovedToBack) {
          REPORT_DUMP(errs() << "Part of non-movable insts: " << *inst << "\n";);
        }
        movedAllIO = false;
        break;
      }
    }
  }
  // only make changes if all IO output insts were movable to last stage
  if (movedAllIO) {
    offPStageWeight = tmpOffPStageWeight;
    for (auto *inst : moveIOToLastSeq) {
      REPORT_DUMP(errs()
            << "Moved output I/O or dependent inst to last sequential stage: "
            << *inst << '\n');
      if (parallelStage->instructions.count(inst))
        parallelStage->instructions.erase(inst);
      else
        firstStage->instructions.erase(inst);
      lastSeqStage->instructions.insert(inst);
    }
  }
}

void PSDSWPCritic::avoidCtrlSpecOnLoopExits(PipelineStrategy &ps, PDG &pdg,
                                            PipelineStage *firstStage,
                                            PipelineStage *parallelStage,
                                            PipelineStage *lastSeqStage) {
  // identify loop exits
  std::unordered_set<Instruction *> loopBounds;
  for (Loop::block_iterator i = loop->block_begin(), e = loop->block_end();
       i != e; ++i) {
    BasicBlock *bb = *i;
    Instruction *term = bb->getTerminator();
    for (unsigned sn = 0, N = term->getNumSuccessors(); sn < N; ++sn) {
      BasicBlock *dest = term->getSuccessor(sn);

      // Loop exit
      if (!loop->contains(dest)) {
        loopBounds.insert(term);
        break;
      }
    }
  }

  for (Instruction *loopExitBr : loopBounds) {

    std::unordered_set<Instruction *> moveToFirstSeq;

    if (!parallelStage->instructions.count(loopExitBr))
      continue;

    if (!pdg.isInternal(loopExitBr))
      continue;

    // check whether control spec is the cheapest remedy to remove LC dep
    // sourcing from branch.
    // if yes, try to move the branch from the parallel stage to the first
    // sequential stage to prevent control spec usage on loop exits (will
    // always cause misspec once per loop invocation)
    auto pdgNode = pdg.fetchConstNode((Instruction *)loopExitBr);
    auto edges = make_range(pdgNode->begin_outgoing_edges(),
                            pdgNode->end_outgoing_edges());
    bool expCtrlSpecNeeded = false;
    for (auto edge : edges) {
      if (edge->isControlDependence() && edge->isRemovableDependence() &&
          edge->isLoopCarriedDependence() &&
          getMinRemovalCost(edge) == EXPENSIVE_CTRL_REMED_COST) {
        expCtrlSpecNeeded = true;
        break;
      }
    }

    // expensive ctrl spec means that there was misspec during profiling
    // namely this loop exit is actually taken during profiling
    if (!expCtrlSpecNeeded)
      continue;

    set<Instruction *> *instsFrontSeqStage =
        (firstStage && firstStage->type == PipelineStage::Sequential)
            ? &firstStage->instructions
            : nullptr;
    set<Instruction *> *instsBackSeqStage =
        (lastSeqStage) ? &lastSeqStage->instructions : nullptr;

    std::unordered_set<Instruction *> tmpInstsMovedToFront;
    std::unordered_set<Instruction *> instsMovedToBack;
    std::unordered_set<DGEdge<Value> *> edgesNotRemoved;
    unsigned long moveFrontCost =
        moveOffStage(pdg, loopExitBr, tmpInstsMovedToFront, instsFrontSeqStage,
                     moveToFirstSeq, instsMovedToBack, instsBackSeqStage,
                     edgesNotRemoved, offPStageWeight, true);

    if (moveFrontCost != ULONG_MAX) {
      offPStageWeight += moveFrontCost;
      if (firstStage && firstStage->type == PipelineStage::Sequential)
        REPORT_DUMP(errs() << "\nMove loop exit branch to first sequential stage to "
                        "avoid control spec remedy on loop exit, "
                     << *loopExitBr << '\n');
      else
        REPORT_DUMP(errs() << "\nMove loop exit branch to replicable stage to "
                        "avoid control spec remedy on loop exit, "
                     << *loopExitBr << '\n');
      for (auto *I : tmpInstsMovedToFront) {
        if (parallelStage->instructions.count(I))
          parallelStage->instructions.erase(I);
        else if (lastSeqStage)
          lastSeqStage->instructions.erase(I);

        if (firstStage && firstStage->type == PipelineStage::Sequential) {
          firstStage->instructions.insert(I);
          REPORT_DUMP(errs() << "Moved loop exit branch or dependent to loop exit "
                          "branch inst to first sequential stage: "
                       << *I << '\n');
        } else {
          parallelStage->replicated.insert(I);
          REPORT_DUMP(errs() << "Moved loop exit branch or dependent to loop exit "
                          "branch inst to replicable stage: "
                       << *I << '\n');
        }
      }
    } else {
      REPORT_DUMP(errs() << "\nNot movable loop exit branch inst along with "
                      "dependent insts to first sequential/replicable stage, "
                   << *loopExitBr << '\n');
      for (auto *I : tmpInstsMovedToFront) {
        REPORT_DUMP(errs() << "Part of non-movable insts: " << *I << "\n";);
      }
    }
  }
}

void PSDSWPCritic::convertRepLightFirstSeqToRepPrefix(
    PipelineStrategy &ps, PipelineStage *firstStage,
    PipelineStage *parallelStage) {

  bool allReplicable = true;
  bool smallWeight = true;
  EdgeWeight seqStageWeight = 0;
  EdgeWeight seqStagePercThreshold = 10; // 10% (maximize DOALL coverage)
  std::unordered_set<Instruction *> seqInsts;
  for (PipelineStage::ISet::const_iterator j = firstStage->instructions.begin(),
                                           z = firstStage->instructions.end();
       j != z; ++j) {
    Instruction *inst = *j;

    seqInsts.insert(inst);
    seqStageWeight += FIXED_POINT * perf->estimate_weight(inst);

    if (!isReplicable(inst)) {
      allReplicable = false;
      break;
    }

    // seq stage should not exceed 2% of parallel stage weight to be
    // lightweight enough for replication. If too large then we are losing the
    // overlapping of seq stage with parallel stage.
    if ((seqStageWeight * 100.0) / parallelStageWeight >
        seqStagePercThreshold) {
      smallWeight = false;
      break;
    }
  }
  if (allReplicable && smallWeight) {
    // first seq stage insts -> parallel stage replicated insts
    for (Instruction *inst : seqInsts) {
      firstStage->instructions.erase(inst);
      parallelStage->replicated.insert(inst);
    }
    // erase completely first sequential stage
    ps.stages.erase(ps.stages.begin());
    ++ps.stages[0].parallel_factor;
  }
}

void PSDSWPCritic::adjustPipeline(PipelineStrategy &ps, PDG &pdg) {

  PipelineStage *parallelStage = nullptr;
  PipelineStage *lastSeqStage = nullptr;
  PipelineStage *firstStage = nullptr;
  // populate the above types of stages (lastSeqStage will remain null for S-P
  // pipeline)
  characterizeStages(ps, &firstStage, &parallelStage, &lastSeqStage);

  if (!parallelStage)
    return;

  // Foreach parallel stage
  // if there is a loop-carried reg dep from a seq stage to parallel stage, then
  // the destination of this dep needs to be moved to the sequential stage. The
  // incoming value to the phi from loop backedge in the parallel stage will be
  // produced by the sequential stage and consumed by the parallel stage in the
  // previous iteration. But each worker will not be executing the previous
  // iteration (except for 1 worker in parallel stage or when within a chunk).
  // Thus, it will never receive the correct value and the incoming to the phi
  // node value will be a stale one. This case was first observed in ks
  // benchmark with phi node on mrPrevA variable.
  if (firstStage && firstStage->type == PipelineStage::Sequential)
    adjustForRegLCFromSeqToPar(ps, pdg, firstStage, parallelStage);

  // temp disable for pldi20. TODO: undo this
  /*
  // if a last sequential stage exists, consider moving all output I/O
  // operations from other stages to the last seq stage
  // Note that output I/O are not expected to source any non-removable
  // dependences. Thus, they could be moved to last seq stage without any check,
  // but they are moved in a generic fashion using the moveOffStage function,
  // just to avoid unexpected errors.
  if (lastSeqStage)
    moveIOToLastSeqStage(ps, pdg, firstStage, parallelStage, lastSeqStage);

  // if control speculation is used for loop exit and this exit is taken during
  // profiling, it is highly probable that we will misspec once and we want try
  // to avoid it
  //    if a first sequential stage exists try to move loop exit branch and
  //      its dependents from the parallel stage to the first stage;
  //    else if first stage is parallel try to move loop exit branch and its
  //      dependents to the replicated prefix of the parallel stage.
  avoidCtrlSpecOnLoopExits(ps, pdg, firstStage, parallelStage, lastSeqStage);
  */

  // If there is a first sequential stage and it is replicable and lightweight
  // convert it to a replicable prefix of the parallel stage
  if (firstStage && firstStage->type == PipelineStage::Sequential)
    convertRepLightFirstSeqToRepPrefix(ps, firstStage, parallelStage);
}

void PSDSWPCritic::populateCriticisms(PipelineStrategy &ps,
                                      Criticisms &criticisms, PDG &pdg) {
  // Add to criticisms all deps which violate pipeline order.

  // Foreach pair (earlier,later) of pipeline stages,
  // where 'earlier' precedes 'later' in the pipeline
  for (PipelineStrategy::Stages::const_iterator i = ps.stages.begin(), e = ps.stages.end(); i != e;
       ++i) {
    const PipelineStage &earlier = *i;
    for (PipelineStrategy::Stages::const_iterator j = i + 1; j != e; ++j) {
      const PipelineStage &later = *j;
      critForPipelineProperty(pdg, earlier, later, criticisms, ps);
    }
  }

  // Add to criticisms all loop-carried deps in parallel stages

  // Foreach parallel stage
  for (PipelineStrategy::Stages::const_iterator i = ps.stages.begin(), e = ps.stages.end(); i != e;
       ++i) {
    const PipelineStage &pstage = *i;
    if (pstage.type != PipelineStage::Parallel)
      continue;

    // If this is a parallel stage,
    // assert that no instruction in this stage
    // is incident on a loop-carried edge.

    critForParallelStageProperty(pdg, pstage, criticisms, ps);
  }
}

// FIXME: need to pass LoopDependenceInfo in, so we can get NOELLE's SCC Manger
CriticRes PSDSWPCritic::getCriticisms(PDG &pdg, Loop *loop) {
  REPORT_DUMP(errs() << "Begin criticisms generation for PS-DSWP critic\n");

  this->loop = loop;
  simplifyPDG(&pdg);

  CriticRes res;

  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  std::unique_ptr<PipelineStrategy> ps =
      std::unique_ptr<PipelineStrategy>(new PipelineStrategy());

  if (!doallAndPipeline(*optimisticPDG, *optimisticSCCDAG, *perf, ps->stages,
                        threadBudget, true /*includeReplicableStages*/,
                        true /*includeParallelStages*/)) {
    REPORT_DUMP(errs() << "PS-DSWP not applicable to " << fcn->getName()
                 << "::" << header->getName()
                 << ": no large parallel stage found (2)\n");
    res.expSpeedup = 0;
    res.ps = nullptr;
    return res;
  }

  offPStageWeight = 0;
  parallelStageWeight = getParalleStageWeight(*ps);

  adjustPipeline(*ps, pdg);

  Criticisms tmpCriticisms;
  populateCriticisms(*ps, tmpCriticisms, pdg);

  // temp disable for pldi20. TODO: undo this
  //avoidExpensiveCriticisms(pdg, *ps, tmpCriticisms);

  // re-populate criticisms after instruction movement across stages to ensure
  // that there are no missing criticisms
  populateCriticisms(*ps, res.criticisms, pdg);

  ps->setValidFor(loop->getHeader());
  ps->assertConsistentWithIR(loop);
  //ps->assertPipelineProperty(optimisticPDG);

  // TODO: check if expansion needs to happen before
  // populateCriticisms, if replicable stages are introduced
  if( ps->expandReplicatedStages() ) {
    ps->assertConsistentWithIR(loop);
    //ps->assertPipelineProperty(pdg);
  }

  REPORT_DUMP(errs() << "\nPS-DSWP applicable to " << fcn->getName() << "::"
               << header->getName() << ": large parallel stage found\n");

  populateCrossStageDependences(*ps, res.criticisms, pdg);

  REPORT_DUMP(ps->dump_pipeline(errs()));

  res.ps = std::move(ps);

  res.expSpeedup = getExpPipelineSpeedup(*res.ps, pdg, loop);

  return res;
}

} // namespace liberty
