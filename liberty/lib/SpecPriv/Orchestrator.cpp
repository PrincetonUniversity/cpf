#define DEBUG_TYPE "orchestrator"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/ValueMap.h"

#include "liberty/SpecPriv/Orchestrator.h"
#include "liberty/SpecPriv/PDG.h"
#include "liberty/SpecPriv/PerformanceEstimator.h"
#include "liberty/SpecPriv/Pipeline.h"
#include "liberty/SpecPriv/Selector.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/Timer.h"

#include <iterator>

namespace liberty {
namespace SpecPriv {
using namespace llvm;

static cl::opt<bool>
    PrintFullPDG("specpriv-print-full-dag-scc", cl::init(false), cl::NotHidden,
                 cl::desc("Print full DAG-SCC-PDG for each hot loop"));

static unsigned filename_nonce = 0;

void printFullPDG(const Loop *loop, const PDG &pdg, const SCCs &sccs,
                  bool bailout = false) {
  if (!PrintFullPDG)
    return;
  const BasicBlock *header = loop->getHeader();
  const Function *fcn = header->getParent();

  std::string hname = header->getName();
  std::string fname = fcn->getName();

  ++filename_nonce;

  char fn[256];
  snprintf(fn, 256, "pdg-%s-%s--%d.dot", fname.c_str(), hname.c_str(),
           filename_nonce);

  char fn2[256];
  snprintf(fn2, 256, "pdg-%s-%s--%d.tred", fname.c_str(), hname.c_str(),
           filename_nonce);

  sccs.print_dot(pdg, fn, fn2, bailout);
  errs() << "See " << fn << " and " << fn2 << '\n';
}

std::set<Remediator_ptr>
Orchestrator::getRemediators(Loop *A, ControlSpeculation *ctrlspec,
                             SmtxSlampSpeculationManager &smtxMan) {
  std::set<Remediator_ptr> remeds;

  // memory specualation remediator
  remeds.insert(
      std::unique_ptr<SmtxSlampRemediator>(new SmtxSlampRemediator(&smtxMan)));

  // add value prediction TODO
  // getAnalysis< HeaderPhiPredictionSpeculation >();

  // control speculation remediator
  ctrlspec->setLoopOfInterest(A->getHeader());
  unique_ptr<ControlSpecRemediator> ctrlSpecRemed =
      std::unique_ptr<ControlSpecRemediator>(
          new ControlSpecRemediator(ctrlspec));
  ctrlSpecRemed->processLoopOfInterest(A);
  remeds.insert(std::move(ctrlSpecRemed));

  /*
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  unique_ptr<ReduxRemediator> reduxRemed = unique_ptr<ReduxRemediator> (new
  ReduxRemediator(&mloops)); reduxRemed->setLoopOfInterest(A);
  remeds.insert(std::move(reduxRemed));

  // TXIO remediator
  remeds.insert(unique_ptr<TXIORemediator> (new TXIORemediator()));
  */

  // commutative libs remediator
  remeds.insert(std::unique_ptr<CommutativeLibsRemediator>(
      new CommutativeLibsRemediator()));

  return remeds;
}

std::set<Critic_ptr> Orchestrator::getCritics() {
  std::set<Critic_ptr> critics;

  // DOALL critic
  critics.insert(std::unique_ptr<DOALLCritic>(new DOALLCritic()));

  return critics;
}

// for now pick the cheapest remedy for each criticism
// TODO: perform instead global reasoning and consider the best set of
// remedies for a given set of criticisms
SelectedRemedies Orchestrator::addressCriticisms(PDG &pdg,
                                                 Criticisms &criticisms) {
  SelectedRemedies sr;
  sr.cost = 0;
  for (Criticism cr : criticisms) {
    Remedies &rs = mapCriticismsToRemeds[cr];
    Remedy_ptr cheapestR = *(rs.begin());
    sr.cost += cheapestR->cost;
    sr.remeds.push_back(cheapestR);

    // remove resolved edge from pdg
    Vertices::ID v, w;
    bool lc;
    DepType dt;
    std::tie(v, w, lc, dt) = cr;
    pdg.removeEdge(v, w, lc, dt);
  }
  return sr;
}

bool Orchestrator::findBestStrategy(
    Loop *loop, LoopAA *loopAA, PerformanceEstimator &perf,
    ControlSpeculation *ctrlspec, SmtxSlampSpeculationManager &smtxMan,
    LoopProfLoad &lpl, PipelineStrategy *strat, unsigned threadBudget,
    bool ignoreAntiOutput, bool includeReplicableStages, bool constrainSubLoops,
    bool abortIfNoParallelStage) {
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();
  Vertices vertices(loop);

  errs() << "\t*** start building PDG, " << loop->getHeader()->getName().str()
         << "\n";
  errs().flush();

  NoControlSpeculation noctrlspec;
  NoPredictionSpeculation nopredspec;

  PDG pdg(vertices, loopAA, noctrlspec, nopredspec, loopAA->getDataLayout(),
          ignoreAntiOutput, constrainSubLoops);

  errs() << "\t*** start printPDG\n";
  errs().flush();

  SCCs sccs(pdg);
  printFullPDG(loop, pdg, sccs);

  // initial computations for perf estimation
  const unsigned loopTime = perf.estimate_loop_weight(loop);
  const unsigned scaledLoopTime = Selector::FixedPoint * loopTime;
  const unsigned depthPenalty =
      Selector::PenalizeLoopNest *
      loop->getLoopDepth(); // break ties with nested loops
  unsigned adjLoopTime = scaledLoopTime;
  if (scaledLoopTime > depthPenalty)
    adjLoopTime = scaledLoopTime - depthPenalty;

  long maxSavings = 0;
  PipelineStrategy *psBest;

  // get all possible criticisms
  Criticisms allCriticisms = Critic::getAllCriticisms(pdg);

  // address all possible criticisms
  std::set<Remediator_ptr> remeds = getRemediators(loop, ctrlspec, smtxMan);
  for (auto remediatorIt = remeds.begin(); remediatorIt != remeds.end();
       ++remediatorIt) {
    Remedies remedies = (*remediatorIt)->satisfy(pdg, allCriticisms);
    for (Remedy_ptr r : remedies)
      for (Criticism c : r->resolvedC)
        mapCriticismsToRemeds[c].insert(r);
  }

  // update pdg with partial remedies info (only the min cost is noted)
  // more info can be included if critics can actually take advantage of them
  // otherwise limit information exposed to Critics and keep them decoupled
  // from Remediators the orchestrator will eventually decide which exact
  // remedies to use
  for (auto &elem : mapCriticismsToRemeds) {
    Criticism c = elem.first;
    Remedies &r = elem.second;
    Remedy_ptr cheapestR = *(r.begin());
    Vertices::ID v, w;
    bool lc;
    DepType dt;
    std::tie(v, w, lc, dt) = c;
    pdg.setRemediatedEdgeCost(cheapestR->cost, v, w, lc, dt);
  }

  // receive actual criticisms from critics given the enhanced pdg
  std::set<Critic_ptr> critics = getCritics();
  for (auto criticIt = critics.begin(); criticIt != critics.end(); ++criticIt) {
    DEBUG(errs() << "Critic " << (*criticIt)->getCriticName() << "\n");
    // create a copy of the pdg for each critic
    // creating copies is just a requirement of the current pipeline
    // suggestion implementation. It could be easily avoided
    Vertices tmpV(pdg.getV().getLoop());
    PDG tmpPdg(pdg, tmpV, pdg.getControlSpeculator(), ignoreAntiOutput);
    CriticRes res = (*criticIt)->getCriticisms(pdg);
    Criticisms &criticisms = res.criticisms;
    int expSpeedup = res.expSpeedup;
    if (expSpeedup == -1) {
      DEBUG(errs() << (*criticIt)->getCriticName() << " not applicable to "
                   << fcn->getName() << "::" << header->getName()
                   << ": not all criticisms are addressable\n");
      continue;
    }

    SelectedRemedies selectedRemedies;
    if (!criticisms.size()) {
      DEBUG(errs() << "No criticisms were generated\n");
      selectedRemedies.cost = 0;
    } else {
      DEBUG(errs() << "All criticisms were addressible\n");
      // orchestrator selects set of remedies to address the given criticisms,
      // computes remedies' total cost
      selectedRemedies = addressCriticisms(tmpPdg, criticisms);
    }

    // get Pipeline stages
    // for DOALL it will return 1 single parallel stage
    PipelineStrategy *ps = new PipelineStrategy();
    sccs.recompute(tmpPdg);
    SCCs::markSequentialSCCs(tmpPdg,sccs);
    bool success =
        Pipeline::suggest(loop, tmpPdg, sccs, perf, *ps, threadBudget,
                          includeReplicableStages, abortIfNoParallelStage);
    if (success) {

      // recompute more precisely expected speedup of this critic
      //(TODO: speedup estimation should probably be done entirely in
      //getCriticisms function)
      long estimatePipelineWeight =
          (long)Selector::FixedPoint * perf.estimate_pipeline_weight(*ps, loop);
      const long wt = adjLoopTime - estimatePipelineWeight;
      long scaledwt = 0;

      if (perf.estimate_loop_weight(loop))
        scaledwt = wt * (double)lpl.getLoopTime(loop->getHeader()) /
                   (double)perf.estimate_loop_weight(loop);

      if (maxSavings < scaledwt - selectedRemedies.cost) {
        psBest = ps;
        maxSavings = scaledwt - selectedRemedies.cost;
      }
    }
  }

  if (maxSavings) {
    strat = psBest;
    return true;
  }

  // no profitable parallelization strategy was found for this loop
  return false;
}

} // namespace SpecPriv
} // namespace liberty
