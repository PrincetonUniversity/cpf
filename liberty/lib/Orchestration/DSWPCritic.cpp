/**
 * Want a balanced stages rather than maximizing the parallell stage like in
 * the PS-DSWP critic
 */
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#define DEBUG_TYPE "dswp-critic"

#include "llvm/IR/Instruction.h"
#include "llvm/Support/Casting.h"

#include "liberty/GraphAlgorithms/Graphs.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/Orchestration/Critic.h"
#include "liberty/Orchestration/DSWPCritic.h"
#include "liberty/Strategy/PipelineStrategy.h"

#include <hash_map>
#include <unordered_set>
#include <utility>

#define FIXED_POINT 1000

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;

// remove removable edges directly from the PDG
PDG DSWPCritic::getOptimisticPdg(PDG &pdg) {
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
  return *optimisticPDG;
}

// cached implementation of get SCC weight
double DSWPCritic::getWeight(SCC *scc) {
  if (weightCache.find(scc) != weightCache.end())
    return weightCache[scc];

  double sumWeight = 0.0;

  for (auto instPair : scc->internalNodePairs()) {
    Instruction *inst = dyn_cast<Instruction>(instPair.first);
    assert(inst);

    sumWeight += perf->estimate_weight(inst);
  }

  sumWeight *= FIXED_POINT;
  weightCache[scc] = sumWeight;

  return sumWeight;
}

std::map<unsigned, unsigned> DSWPCritic::getIncomingCountMap(SCCDAG &sccdag) {
  std::map<unsigned, unsigned> incomingCountMap;

  for (auto *scc : sccdag.getSCCs()) {
    auto idx = sccdag.getSCCIndex(scc);
    incomingCountMap[idx] = sccdag.fetchNode(scc)->numIncomingEdges();
  }

  return incomingCountMap;
}

// DEBUG ONLY
void printIncomingCountMap(std::map<unsigned, unsigned> &incomingCountMap) {
   for (auto &[k, v] : incomingCountMap) {
     errs() << k << "->" << v << "\n";
   }
}

// find the one with largest weight and free-standing node
// FIXME: this is super inefficient
SCC *DSWPCritic::getNextLargestFreeStandingSCC(
    SCCDAG &sccdag, std::map<unsigned, unsigned> &incomingCountMap) {
  double largestWeight = -1;
  auto largestIdx = -1;
  SCC *largestSCC = nullptr;

  LLVM_DEBUG(printIncomingCountMap(incomingCountMap));

  for (auto *scc : sccdag.getSCCs()) {
    auto idx = sccdag.getSCCIndex(scc);
    // free-standing node

    if (incomingCountMap[idx] != 0)
      continue;

    auto weight = getWeight(scc);

    // get largest weight
    if (weight > largestWeight) {
      largestWeight = weight;
      largestIdx = idx;
      largestSCC = scc;
    }
  }

  return largestSCC;
}

void DSWPCritic::updateIncomingCountMap(SCCDAG &sccdag,
                            std::map<unsigned, unsigned> &incomingCountMap,
                            SCC *removedScc) {
  auto idx = sccdag.getSCCIndex(removedScc);
  incomingCountMap[idx] = -1;

  LLVM_DEBUG(errs() << "SCC " << idx << " has "
                    << sccdag.fetchNode(removedScc)->numOutgoingEdges()
                    << " outgoing edges\n";);

  // decrement incoming counts for the largest node
  for (auto *edge : sccdag.fetchNode(removedScc)->getOutgoingEdges()) {
    // find the index
    SCC *outgoingSCC = edge->getIncomingT(); // incoming is "TO"

    auto idx = sccdag.getSCCIndex(outgoingSCC);
    LLVM_DEBUG(errs() << "decrementing " << idx << "\n";);
    incomingCountMap[idx]--;
  }
}

void DSWPCritic::populateCrossStageDependences(PipelineStrategy &ps, PDG &pdg) {

  for (PipelineStage &currStage : ps.stages) {
    // for all instructions
    auto allInsts = currStage.instructions;

    for (Instruction *src : allInsts) {
      auto srcNode = pdg.fetchNode(src);

      for (auto edge : srcNode->getOutgoingEdges()) {
        auto *dst = dyn_cast<Instruction>(edge->getIncomingT());

        // the dst is in a different stage
        if (allInsts.count(dst))
          continue;

        // control dep
        if (edge->isControlDependence() && src->isTerminator()) {
          ps.crossStageDeps.push_back(CrossStageDependence(src, dst, edge));
        }
        // mem dep
        else if (edge->isMemoryDependence() && edge->isRAWDependence()) {
          ps.crossStageDeps.push_back(CrossStageDependence(src, dst, edge));
        }
      }
    }
  }
}

CriticRes DSWPCritic::getCriticisms(PDG &pdg, Loop *loop) {
  this->loop = loop;
  CriticRes res;

  // get optimistic PDG and SCCDAG
  auto optimisticPDG = getOptimisticPdg(pdg);
  auto optimisticSCCDAG = new SCCDAG(&optimisticPDG);

  // invalidate cache
  weightCache.clear();

  // get the weights out
  /*
   *    VertexSet vertices;
   *    VertexWeights weights;
   *    Edges edges;
   *
   *    for (SCC *scc : optimisticSCCDAG->getSCCs()) {
   *      vertices.push_back(optimisticSCCDAG->getSCCIndex(scc));
   *      auto weight = getWeight(scc);
   *      weights.push_back(weight);
   *    }
   *
   *    for (auto sccEdge : optimisticSCCDAG->getEdges()) {
   *      auto nodePair = sccEdge->getNodePair();
   *      Edge edge =
   *          make_pair(optimisticSCCDAG->getSCCIndex(nodePair.first->getT()),
   *                    optimisticSCCDAG->getSCCIndex(nodePair.second->getT()));
   *      edges.insert(edge);
   *    }
   *
   */
  // split the SCC DAG into multiple stages with the constant number of
  // stages in the most balanced way

  unsigned int numThread = min(threadBudget, optimisticSCCDAG->numNodes());
  double totalWeight = 0;

  // get total weight
  for (auto *scc : optimisticSCCDAG->getSCCs()) {
    totalWeight += getWeight(scc);
  }

  // the target per thread
  double targetWeightPerThread = totalWeight / numThread;

  LLVM_DEBUG(errs() << "Weights: \n"
                    << "Total weight: " << totalWeight
                    << "\nTarget weight: " << targetWeightPerThread << "\n");

  // get the partition plan
  std::vector<SCCDAG::SCCSet> partitionStages;

  std::map<unsigned, unsigned> incomingCountMap =
      getIncomingCountMap(*optimisticSCCDAG);

  // FIXME: stupid strategy - fill one stage till full (over
  // targetWeightPerThread) This can be done more efficiently by creating a
  // topological and weighted ordered list of all SCC Then partition is s.t. the
  // largest set is minimized
  for (int i = 0; i < numThread; i++) {
    SCCDAG::SCCSet currSccSet;

    auto currWeight = 0;

    while (currWeight < targetWeightPerThread) {
      SCC *largestScc =
          getNextLargestFreeStandingSCC(*optimisticSCCDAG, incomingCountMap);
      if (!largestScc)
        break;
      currSccSet.push_back(largestScc);
      currWeight += getWeight(largestScc);
      updateIncomingCountMap(*optimisticSCCDAG, incomingCountMap, largestScc);
    }

    // push the rest in the last one
    if (i == numThread - 1) {
      /*
       *SCC *scc =
       *    getNextLargestFreeStandingSCC(*optimisticSCCDAG, incomingCountMap);
       *while (scc) {
       *  currSccSet.push_back(scc);
       *  updateIncomingCountMap(*optimisticSCCDAG, incomingCountMap, scc);
       *}
       */

       for (auto *scc : optimisticSCCDAG->getSCCs()) {
         auto idx = optimisticSCCDAG->getSCCIndex(scc);

         if (incomingCountMap[idx] != -1) {
           currSccSet.push_back(scc);
         }
       }
    }

    if (!currSccSet.empty())
      partitionStages.push_back(currSccSet);

    LLVM_DEBUG(errs() << "Stage " << i << " has " << currSccSet.size()
                      << " SCCs\n");
  }

  // create PipelineStrategy from SCC partition
  std::unique_ptr<PipelineStrategy> ps =
      std::unique_ptr<PipelineStrategy>(new PipelineStrategy());

  for (auto &stage : partitionStages) {
    ps->stages.push_back(PipelineStage(PipelineStage::Sequential, optimisticPDG,
                                       stage));
  }

  ps->setValidFor(loop->getHeader());
  ps->assertConsistentWithIR(loop);

  populateCrossStageDependences(*ps, optimisticPDG);

  res.ps = std::move(ps);
  res.expSpeedup = getExpPipelineSpeedup(*res.ps, optimisticPDG, loop);

  // need to populate criticisms
  res.criticisms.clear(); // no criticisms generated

  return res;
}

} // namespace liberty
