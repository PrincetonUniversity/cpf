#define DEBUG_TYPE "critic"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/SpecPriv/Critic.h"

#define DEFAULT_THREADS 24

namespace liberty {
using namespace llvm;

// produce all the possible criticisms
Criticisms Critic::getAllCriticisms(const PDG &pdg) {
  Criticisms criticisms;
  for (auto edge : make_range(pdg.begin_edges(), pdg.end_edges())) {

    DEBUG(errs() << "  Found new edge(s) from " << *edge->getOutgoingT()
                 << " to " << *edge->getIncomingT() << '\n');

    criticisms.insert(edge);
  }
  return criticisms;
}

void Critic::printCriticisms(raw_ostream &fout, Criticisms &cs, const PDG &pdg) {
  fout << "-===============================================================-\n";
  fout << "Printing Criticisms\n";
  for (auto c : cs) {
    fout << *c->getOutgoingT() << " -> " << *c->getIncomingT()
         << " (loopCarried=" << c->isLoopCarriedDependence() << ")\n";
  }
  fout << "-===============================================================-\n";
}

// Note that just getting score = perf.estimate_pipeline_weight( stages ) should
// be enough to compare different parallelization plans of the same loop. Loop
// related computations are only needed for the final score computation used in
// loop selection.
long Critic::getExpPipelineSpeedup(const ParallelizationPlan &ps,
                                   const PDG &pdg, Loop *loop) {
  const unsigned loopTime = perf->estimate_loop_weight(loop);
  const unsigned scaledLoopTime = Selector::FixedPoint * loopTime;
  const unsigned depthPenalty =
      Selector::PenalizeLoopNest *
      loop->getLoopDepth(); // break ties with nested loops
  unsigned adjLoopTime = scaledLoopTime;
  if (scaledLoopTime > depthPenalty)
    adjLoopTime = scaledLoopTime - depthPenalty;

  long estimatePipelineWeight =
      (long)Selector::FixedPoint * perf->estimate_pipeline_weight(ps, loop);
  const long wt = adjLoopTime - estimatePipelineWeight;
  long scaledwt = 0;

  if (perf->estimate_loop_weight(loop))
    scaledwt = wt * (double)lpl->getLoopTime(loop->getHeader()) /
               (double)perf->estimate_loop_weight(loop);

  return scaledwt;
}

PDG *getExpectedPdg(PDG &pdg, Criticisms &criticisms,
                    std::set<DGEdge<Value> *> boundedIVRemovableEdges) {
  std::vector<Value *> iPdgNodes;
  for (auto node : pdg.internalNodePairs()) {
    iPdgNodes.push_back(node.first);
  }

  PDG *expPdg = pdg.createSubgraphFromValues(iPdgNodes, false);

  for (auto edge : boundedIVRemovableEdges)
    expPdg->removeEdge(edge);

  // at this point expPdg can be used for new remediators possibilities
  // pdg is annotated with costs and bounded IV deps are removed

  for (auto cr : criticisms)
    expPdg->removeEdge(cr);

  return expPdg;
}

std::unique_ptr<ParallelizationPlan>
DOALLCritic::getDOALLStrategy(PDG &pdg, Loop *loop) {
  std::unique_ptr<ParallelizationPlan> ps =
      std::unique_ptr<ParallelizationPlan>(new ParallelizationPlan());

  SCCDAG *loopSCCDAG = SCCDAG::createSCCDAGFrom(&pdg);

  for (auto edge : make_range(pdg.begin_edges(), pdg.end_edges())) {
    if (edge->isLoopCarriedDependence())
      return nullptr;
  }

  SCCDAG::SCCSet maxParallel;
  for (auto pair : loopSCCDAG->internalNodePairs()) {
    auto scc = pair.first;
    maxParallel.insert(scc);
  }

  PipelineStage parallel_stage =
      PipelineStage(PipelineStage::Parallel, pdg, maxParallel);

  ps->stages.push_back(parallel_stage);

  //bool success = DOALL::suggest(loop, pdg, *loopSCCDAG, *perf, *ps, threadBudget);

  //if (!success)
  //  return nullptr;

  return ps;
}

CriticRes DOALLCritic::getCriticisms(PDG &pdg, Loop *loop,
                                     LoopDependenceInfo &ldi) {
  // Produce criticisms using the applicability guard of DOALL
  DEBUG(errs() << "Begin criticisms generation for DOALL critic\n");

  CriticRes res;

  if (ldi.numberOfExits() > 1) {
  	DEBUG(errs() << "DOALL:   More than 1 loop exit blocks\n");
  }

  if (!ldi.sccdagAttrs.areAllLiveOutValuesReducable(ldi.environment)) {
  	DEBUG(errs() << "DOALL:   Some post environment value is not reducable\n");
  }

  // detect counted loop in order to ignore related dependences
  auto headerBr = ldi.header->getTerminator();
  auto headerSCC = ldi.loopSCCDAG->sccOfValue(headerBr);
  std::set<DGEdge<Value> *> boundedIVRemovableEdges;
  if (ldi.sccdagAttrs.isLoopGovernedByIV() &&
      ldi.sccdagAttrs.sccIVBounds.find(headerSCC) !=
          ldi.sccdagAttrs.sccIVBounds.end()) {
    for (auto edge : headerSCC->getEdges()) {
      if (headerSCC->isInternal(edge->getOutgoingT()) &&
          headerSCC->isInternal(edge->getIncomingT()) &&
          edge->isLoopCarriedDependence())
        boundedIVRemovableEdges.insert(edge);
    }
  }

  for (auto edge : make_range(pdg.begin_edges(), pdg.end_edges())) {

    if (edge->isLoopCarriedDependence() && !boundedIVRemovableEdges.count(edge)) {

      DEBUG(errs() << "  Found new DOALL criticism loop-carried from "
                   << *edge->getOutgoingT() << " to " << *edge->getIncomingT()
                   << '\n');

      // check if this edge is removable
      if (edge->isRemovable()) {
        // criticism cannot be remedied. Abort
        DEBUG(errs() << "Cannot remove loop-carried edge(s) from "
                     << *edge->getOutgoingT() << " to " << *edge->getIncomingT()
                     << '\n');
        //res.expSpeedup = -1;
        //return res;
        continue;
      }

      res.criticisms.insert(edge);
    }
  }

  // get expected pdg if all the criticisms are satisfied
  PDG *expPdg = getExpectedPdg(pdg, res.criticisms, boundedIVRemovableEdges);

  res.ps = getDOALLStrategy(*expPdg, loop);

  if (res.ps)
    res.expSpeedup = getExpPipelineSpeedup(*res.ps, *expPdg, loop);
  else
    res.expSpeedup = -1;

  delete expPdg;

  return res;
}

} // namespace liberty
