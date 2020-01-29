#define DEBUG_TYPE "critic"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Orchestration/Critic.h"

#define DEFAULT_THREADS 24

namespace liberty {
using namespace llvm;

// produce all the possible criticisms
Criticisms Critic::getAllCriticisms(const PDG &pdg) {
  Criticisms criticisms;
  for (auto edge : make_range(pdg.begin_edges(), pdg.end_edges())) {

    if (!pdg.isInternal(edge->getIncomingT()) ||
        !pdg.isInternal(edge->getOutgoingT()))
      continue;

    //LLVM_DEBUG(errs() << "  Found new edge(s) from " << *edge->getOutgoingT()
    //             << " to " << *edge->getIncomingT() << '\n');

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

const unsigned Critic::FixedPoint(1000);
const unsigned Critic::PenalizeLoopNest( Critic::FixedPoint*10 );

// Note that just getting score = perf.estimate_pipeline_weight( stages ) should
// be enough to compare different parallelization plans of the same loop. Loop
// related computations are only needed for the final score computation used in
// loop selection.
unsigned long Critic::getExpPipelineSpeedup(const ParallelizationPlan &ps,
                                            const PDG &pdg, Loop *loop) {
  const long unsigned loopTime = perf->estimate_loop_weight(loop);
  const long unsigned scaledLoopTime = FixedPoint * loopTime;
  const unsigned depthPenalty =
      PenalizeLoopNest * loop->getLoopDepth(); // break ties with nested loops
  long unsigned adjLoopTime = scaledLoopTime;
  if (scaledLoopTime > depthPenalty)
    adjLoopTime = scaledLoopTime - depthPenalty;

  long unsigned estimatePipelineWeight =
      (long)FixedPoint * perf->estimate_pipeline_weight(ps, loop);
  const unsigned long wt = adjLoopTime - estimatePipelineWeight;

  /*
  long scaledwt = 0;

  if (perf->estimate_loop_weight(loop))
    scaledwt = wt * (double)lpl->getLoopTime(loop->getHeader()) /
               (double)perf->estimate_loop_weight(loop);

  return scaledwt;
  */
  return wt;
}

PDG *getExpectedPdg(PDG &pdg, Criticisms &criticisms) {

  // instead of modifying pdg in-place, create new copy of the pdg without the
  // removable loop-carried deps
  /*
  for (auto cr : criticisms) {
    //LLVM_DEBUG(errs() << " Removing DOALL criticism loop-carried from "
    //             << *cr->getOutgoingT() << " to " << *cr->getIncomingT()
    //             << '\n');

    pdg.removeEdge(cr);
  }
  */

  std::vector<Value *> loopInternals;
  for (auto internalNode : pdg.internalNodePairs()) {
    loopInternals.push_back(internalNode.first);
  }
  PDG *expectedPDG = pdg.createSubgraphFromValues(loopInternals, false);

  std::vector<DGEdge<Value> *> removableLCEdges;
  for (auto edge : expectedPDG->getEdges()) {
    if (edge->isLoopCarriedDependence() && edge->isRemovableDependence())
      removableLCEdges.push_back(edge);
  }

  for (auto edge : removableLCEdges) {
    // LLVM_DEBUG(errs() << " Removing loop-carried dep from "
    //             << *edge->getOutgoingT() << " to " << *edge->getIncomingT()
    //             << '\n');
    expectedPDG->removeEdge(edge);
  }
  return expectedPDG;
}

std::unique_ptr<ParallelizationPlan>
DOALLCritic::getDOALLStrategy(PDG &pdg, Loop *loop) {
  std::unique_ptr<ParallelizationPlan> ps =
      std::unique_ptr<ParallelizationPlan>(new ParallelizationPlan());

  //SCCDAG *loopSCCDAG = new SCCDAG(&pdg);

  for (auto edge : make_range(pdg.begin_edges(), pdg.end_edges())) {

    if (!pdg.isInternal(edge->getIncomingT()) ||
        !pdg.isInternal(edge->getOutgoingT()))
      continue;

    if (edge->isLoopCarriedDependence()) {
      LLVM_DEBUG(errs() << "No DOALL strategy possible since there is at least one "
                      "loop carried edge."
                   << '\n');

      return nullptr;
    }
  }

  /*
  SCCDAG::SCCSet maxParallel;
  for (auto pair : loopSCCDAG->internalNodePairs()) {
    auto scc = pair.first;
    maxParallel.insert(scc);
  }
  */

  //PipelineStage parallel_stage =
  //    PipelineStage(PipelineStage::Parallel, pdg, maxParallel);

  std::vector<Instruction *> parallelInstV;
  for (auto instPair : pdg.internalNodePairs()) {
    Instruction *inst = dyn_cast<Instruction>(instPair.first);
    assert(inst);
    parallelInstV.push_back(inst);
  }

  PipelineStage parallel_stage = PipelineStage(PipelineStage::Parallel, parallelInstV);

  parallel_stage.parallel_factor = threadBudget;

  ps->stages.push_back(parallel_stage);

  //bool success = DOALL::suggest(loop, pdg, *loopSCCDAG, *perf, *ps, threadBudget);

  //if (!success)
  //  return nullptr;

  return ps;
}

CriticRes DOALLCritic::getCriticisms(PDG &pdg, Loop *loop,
                                     LoopDependenceInfo &ldi) {
  // Produce criticisms using the applicability guard of DOALL
  LLVM_DEBUG(errs() << "Begin criticisms generation for DOALL critic\n");

  CriticRes res;

  /*
  if (ldi.numberOfExits() > 1) {
  	LLVM_DEBUG(errs() << "DOALL:   More than 1 loop exit blocks\n");
  }

  if (!ldi.sccdagAttrs.areAllLiveOutValuesReducable(ldi.environment)) {
  	LLVM_DEBUG(errs() << "DOALL:   Some post environment value is not reducable\n");
  }
  */

  unsigned criticismsTotal = 0;
  unsigned criticismsCovered = 0;

  for (auto edge : make_range(pdg.begin_edges(), pdg.end_edges())) {

    if (!pdg.isInternal(edge->getIncomingT()) ||
        !pdg.isInternal(edge->getOutgoingT()))
      continue;

    if (edge->isLoopCarriedDependence()) {

      ++criticismsTotal;

      //LLVM_DEBUG(errs() << "  Found new DOALL criticism loop-carried from "
      //             << *edge->getOutgoingT() << " to " << *edge->getIncomingT()
      //             << '\n');

      // check if this edge is removable
      if (!edge->isRemovableDependence()) {
        // criticism cannot be remedied.
        LLVM_DEBUG(errs() << "Cannot remove loop-carried edge(s) from "
                     << *edge->getOutgoingT() << " to " << *edge->getIncomingT()
                     << '\n');
        //res.expSpeedup = -1;
        //return res;
        continue;
      }

      ++criticismsCovered;
      res.criticisms.insert(edge);
    }
  }
  BasicBlock *loopH = loop->getHeader();
  Function *loopF = loopH->getParent();
  double percentageCovered = (100.0 * criticismsCovered) / criticismsTotal;
  LLVM_DEBUG(errs() << "\nCoverage of loop-carried dependences for hot loop "
               << loopF->getName() << " :: " << loopH->getName() << " "
               << "covered=" << criticismsCovered
               << ", total=" << criticismsTotal << " , percentage="
               << format("%.2f", percentageCovered) << "%\n\n");

  // TODO: create deep copy of the PDG with only its internal nodes when there
  // are more than one critics.
  // pdg.createSubgraphFromValues is not appropriate since it creates new edges,
  // thus losing the connection between criticisms in the original pdg and edges
  // in the copy. Need to add new copy function when multiple critics are
  // available
  //
  // get expected pdg if all the criticisms are satisfied
  PDG *expectedPDG = getExpectedPdg(pdg, res.criticisms);

  res.ps = getDOALLStrategy(*expectedPDG, loop);

  if (res.ps)
    res.expSpeedup = getExpPipelineSpeedup(*res.ps, *expectedPDG, loop);
  else
    res.expSpeedup = 0;

  delete expectedPDG;

  return res;
}

} // namespace liberty
