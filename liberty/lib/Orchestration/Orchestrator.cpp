#define DEBUG_TYPE "orchestrator"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/ValueMap.h"

#include "liberty/Orchestration/Orchestrator.h"
#include "liberty/Strategy/PerformanceEstimator.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/Timer.h"

#include <iterator>

namespace liberty {
namespace SpecPriv {
using namespace llvm;

/*
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
*/

std::set<Remediator_ptr>
Orchestrator::getRemediators(Loop *A, PDG *pdg, ControlSpeculation *ctrlspec,
                             PredictionSpeculation *loadedValuePred,
                             PredictionSpeculation *headerPhiPred,
                             ModuleLoops &mloops, LoopDependenceInfo &ldi,
                             SmtxSlampSpeculationManager &smtxMan,
                             const Read &rd, const HeapAssignment &asgn,
                             LocalityAA &localityaa, LoopAA *loopAA) {
  std::set<Remediator_ptr> remeds;

  // memory specualation remediator
  remeds.insert(std::make_unique<SmtxSlampRemediator>(&smtxMan));

  // header phi value prediction
  remeds.insert(std::make_unique<HeaderPhiPredRemediator>(headerPhiPred));

  // Loop-Invariant Loaded-Value Prediction
  remeds.insert(std::make_unique<LoadedValuePredRemediator>(loadedValuePred));

  // control speculation remediator
  ctrlspec->setLoopOfInterest(A->getHeader());
  auto ctrlSpecRemed = std::make_unique<ControlSpecRemediator>(ctrlspec);
  ctrlSpecRemed->processLoopOfInterest(A);
  remeds.insert(std::move(ctrlSpecRemed));

  // reduction remediator
  auto reduxRemed = std::make_unique<ReduxRemediator>(&mloops, &ldi, loopAA);
  reduxRemed->setLoopOfInterest(A);
  remeds.insert(std::move(reduxRemed));

  // privitization remediator
  auto privRemed = std::make_unique<PrivRemediator>();
  privRemed->setPDG(pdg);
  remeds.insert(std::move(privRemed));

  // counted induction variable remediator
  remeds.insert(std::make_unique<CountedIVRemediator>(&ldi));

  // TXIO remediator
  remeds.insert(std::make_unique<TXIORemediator>());

  // separation logic remediator (Privateer PLDI '12)
  remeds.insert(std::make_unique<LocalityRemediator>(rd, asgn, localityaa));

  // memory versioning remediator
  remeds.insert(std::make_unique<MemVerRemediator>());

  // commutative libs remediator
  //remeds.insert(std::unique_ptr<CommutativeLibsRemediator>(
  //    new CommutativeLibsRemediator()));

  return remeds;
}

std::set<Critic_ptr> Orchestrator::getCritics(PerformanceEstimator *perf,
                                              unsigned threadBudget,
                                              LoopProfLoad *lpl) {
  std::set<Critic_ptr> critics;

  // DOALL critic
  critics.insert(std::make_shared<DOALLCritic>(perf, threadBudget, lpl));

  return critics;
}

// for now pick the cheapest remedy for each criticism
// TODO: perform instead global reasoning and consider the best set of
// remedies for a given set of criticisms
void Orchestrator::addressCriticisms(SelectedRemedies &selectedRemedies,
                                     long &selectedRemediesCost,
                                     Criticisms &criticisms) {
  DEBUG(errs() << "\n-====================================================-\n");
  DEBUG(errs() << "Selected Remedies:\n");
  for (Criticism *cr : criticisms) {
    Remedies &rs = mapCriticismsToRemeds[cr];
    Remedy_ptr cheapestR = *(rs.begin());
    if (!selectedRemedies.count(cheapestR))
      selectedRemediesCost += cheapestR->cost;
    selectedRemedies.insert(cheapestR);
    DEBUG(errs() << "----------------------------------------------------\n");
    DEBUG(errs() << cheapestR->getRemedyName()
                 << " chosen to address criticicm:\n"
                 << *cr->getOutgoingT() << " ->\n"
                 << *cr->getIncomingT() << "\n");
    if (rs.size() > 1) {
      DEBUG(errs() << "\nAlternative remedies for the same criticism: ");
      auto itR = rs.begin();
      while(itR != rs.end()) {
        if (*itR == cheapestR) {
          ++itR;
          continue;
        }
        DEBUG(errs() << (*itR)->getRemedyName());
        if ((++itR) != rs.end())
          DEBUG(errs() << ", ");
        else
          DEBUG(errs() << "\n");
      }
    }
    DEBUG(errs()
          << "------------------------------------------------------\n\n");
  }
  DEBUG(errs() << "-====================================================-\n\n");
}

bool Orchestrator::findBestStrategy(
    Loop *loop, llvm::PDG &pdg, LoopDependenceInfo &ldi,
    PerformanceEstimator &perf, ControlSpeculation *ctrlspec,
    PredictionSpeculation *loadedValuePred,
    PredictionSpeculation *headerPhiPred, ModuleLoops &mloops,
    SmtxSlampSpeculationManager &smtxMan, const Read &rd,
    const HeapAssignment &asgn, LocalityAA &localityaa, LoopAA *loopAA,
    LoopProfLoad &lpl, std::unique_ptr<PipelineStrategy> &strat,
    std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
    unsigned threadBudget, bool ignoreAntiOutput, bool includeReplicableStages,
    bool constrainSubLoops, bool abortIfNoParallelStage) {
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  DEBUG(errs() << "Start of findBestStrategy for loop " << fcn->getName()
               << "::" << header->getName() << "\n");

  // create pdg without live-in and live-out values (aka external to the loop nodes)
  std::vector<Value *> iPdgNodes;
  for (auto node : pdg.internalNodePairs()) {
    iPdgNodes.push_back(node.first);
  }
  PDG *ipdg = pdg.createSubgraphFromValues(iPdgNodes, false);

  long maxSavings = 0;

  // get all possible criticisms
  Criticisms allCriticisms = Critic::getAllCriticisms(*ipdg);

  // address all possible criticisms
  std::set<Remediator_ptr> remeds =
      getRemediators(loop, ipdg, ctrlspec, loadedValuePred, headerPhiPred,
                     mloops, ldi, smtxMan, rd, asgn, localityaa, loopAA);
  for (auto remediatorIt = remeds.begin(); remediatorIt != remeds.end();
       ++remediatorIt) {
    Remedies remedies = (*remediatorIt)->satisfy(*ipdg, loop, allCriticisms);
    for (Remedy_ptr r : remedies) {
      auto rCost = make_shared<unsigned>();
      mapRemedEdgeCostsToRemedies[rCost] = r;
      for (Criticism *c : r->resolvedC) {
        mapCriticismsToRemeds[c].insert(r);
        c->insertEdgeRemedCost(rCost);
      }
    }
  }

  // receive actual criticisms from critics given the enhanced pdg
  std::set<Critic_ptr> critics = getCritics(&perf, threadBudget, &lpl);
  for (auto criticIt = critics.begin(); criticIt != critics.end(); ++criticIt) {
    DEBUG(errs() << "Critic " << (*criticIt)->getCriticName() << "\n");
    CriticRes res = (*criticIt)->getCriticisms(*ipdg, loop, ldi);
    Criticisms &criticisms = res.criticisms;
    long expSpeedup = res.expSpeedup;

    if (expSpeedup == -1) {
      DEBUG(errs() << (*criticIt)->getCriticName()
                   << " not applicable/profitable to " << fcn->getName()
                   << "::" << header->getName()
                   << ": not all criticisms are addressable\n");
      continue;
    }

    std::unique_ptr<SelectedRemedies> selectedRemedies =
        std::unique_ptr<SelectedRemedies>(new SelectedRemedies());
    long selectedRemediesCost = 0;
    if (!criticisms.size()) {
      DEBUG(errs() << "No criticisms generated\n");
    } else {
      DEBUG(errs() << "Addressible criticisms\n");
      // orchestrator selects set of remedies to address the given criticisms,
      // computes remedies' total cost
      addressCriticisms(*selectedRemedies, selectedRemediesCost, criticisms);
    }

    long savings = expSpeedup - selectedRemediesCost;
    if (maxSavings < savings) {
      maxSavings = savings;
      strat = std::move(res.ps);
      sRemeds = std::move(selectedRemedies);
      sCritic = *criticIt;
    }
  }

  delete ipdg;

  if (maxSavings)
    return true;

  // no profitable parallelization strategy was found for this loop
  return false;
}

} // namespace SpecPriv
} // namespace liberty
