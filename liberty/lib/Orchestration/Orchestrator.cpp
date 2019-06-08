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

std::vector<Remediator_ptr> Orchestrator::getRemediators(
    Loop *A, PDG *pdg, ControlSpeculation *ctrlspec,
    PredictionSpeculation *loadedValuePred,
    PredictionSpeculation *headerPhiPred, ModuleLoops &mloops,
    LoopDependenceInfo &ldi, SmtxSlampSpeculationManager &smtxMan,
    SmtxSpeculationManager &smtxLampMan, const Read &rd,
    const HeapAssignment &asgn, Pass &proxy, LoopAA *loopAA) {
  std::vector<Remediator_ptr> remeds;

  // reduction remediator
  auto reduxRemed = std::make_unique<ReduxRemediator>(&mloops, &ldi, loopAA, pdg);
  reduxRemed->setLoopOfInterest(A);
  remeds.push_back(std::move(reduxRemed));

  // separation logic remediator (Privateer PLDI '12)
  remeds.push_back(std::make_unique<LocalityRemediator>(rd, asgn, proxy));

  // memory specualation remediator (with SLAMP)
  remeds.push_back(std::make_unique<SmtxSlampRemediator>(&smtxMan));

  // memory specualation remediator 2 (with LAMP)
  //remeds.push_back(std::make_unique<SmtxLampRemediator>(&smtxLampMan, proxy));

  // header phi value prediction
  //remeds.push_back(std::make_unique<HeaderPhiPredRemediator>(headerPhiPred));

  // Loop-Invariant Loaded-Value Prediction
  remeds.push_back(std::make_unique<LoadedValuePredRemediator>(loadedValuePred));

  // control speculation remediator
  ctrlspec->setLoopOfInterest(A->getHeader());
  auto ctrlSpecRemed = std::make_unique<ControlSpecRemediator>(ctrlspec);
  ctrlSpecRemed->processLoopOfInterest(A);
  remeds.push_back(std::move(ctrlSpecRemed));

  // privitization remediator
  auto privRemed = std::make_unique<PrivRemediator>();
  privRemed->setPDG(pdg);
  remeds.push_back(std::move(privRemed));

  // counted induction variable remediator
  remeds.push_back(std::make_unique<CountedIVRemediator>(&ldi));

  // TXIO remediator
  remeds.push_back(std::make_unique<TXIORemediator>());

  // memory versioning remediator
  remeds.push_back(std::make_unique<MemVerRemediator>());

  // commutative libs remediator
  remeds.push_back(std::make_unique<CommutativeLibsRemediator>());

  return remeds;
}

std::vector<Critic_ptr> Orchestrator::getCritics(PerformanceEstimator *perf,
                                                 unsigned threadBudget,
                                                 LoopProfLoad *lpl) {
  std::vector<Critic_ptr> critics;

  // PS-DSWP critic
  critics.push_back(std::make_shared<PSDSWPCritic>(perf, threadBudget, lpl));

  // DOALL critic (covered by PS-DSWP critic)
  //critics.push_back(std::make_shared<DOALLCritic>(perf, threadBudget, lpl));

  return critics;
}

void Orchestrator::printRemediatorSelectionCnt() {
  DEBUG(errs() << "Selected Remediators:\n\n");
  for (auto const &it : remediatorSelectionCnt) {
    DEBUG(errs() << it.first << " was selected " << it.second << " times\n");
  }
}

void Orchestrator::printRemedies(Remedies &rs, bool selected) {
  DEBUG(errs() << "( ");
  auto itRs = rs.begin();
  while(itRs != rs.end()) {
    StringRef remedyName = (*itRs)->getRemedyName();
    if (remedyName.equals("locality-remedy")) {
      LocalityRemedy *localityRemed = (LocalityRemedy *)&*(*itRs);
      remedyName = localityRemed->getLocalityRemedyName();
    }
    DEBUG(errs() << remedyName);
    if (selected) {
      if (remediatorSelectionCnt.count(remedyName.str()))
        ++remediatorSelectionCnt[remedyName.str()];
      else
        remediatorSelectionCnt[remedyName.str()] = 1;
    }
    if ((++itRs) != rs.end())
      DEBUG(errs() << ", ");
  }
  DEBUG(errs() << " )");
}

void Orchestrator::printSelected(SetOfRemedies &sors,
                                 const Remedies_ptr &selected, Criticism &cr) {
  DEBUG(errs() << "----------------------------------------------------\n");
  printRemedies(*selected, true);
  DEBUG(errs() << " chosen to address criticicm ";
        if (cr.isControlDependence()) errs() << "(Control, "; else {
          if (cr.isMemoryDependence())
            errs() << "(Mem, ";
          else
            errs() << "(Reg, ";
          if (cr.isWARDependence())
            errs() << "WAR, ";
          else if (cr.isWAWDependence())
            errs() << "WAW, ";
          else if (cr.isRAWDependence())
            errs() << "RAW, ";
        }
        if (cr.isLoopCarriedDependence()) errs() << "LC)";
        else errs() << "II)";

        errs() << ":\n"
               << *cr.getOutgoingT();
        if (Instruction *outgoingI = dyn_cast<Instruction>(cr.getOutgoingT()))
            liberty::printInstDebugInfo(outgoingI);
        errs() << " ->\n"
               << *cr.getIncomingT();
        if (Instruction *incomingI = dyn_cast<Instruction>(cr.getIncomingT()))
            liberty::printInstDebugInfo(incomingI);
        errs() << "\n";);
  if (sors.size() > 1) {
    DEBUG(errs() << "\nAlternative remedies for the same criticism: ");
    auto itR = sors.begin();
    while (itR != sors.end()) {
      if (*itR == selected) {
        ++itR;
        continue;
      }
      printRemedies(**itR, false);
      if ((++itR) != sors.end())
        DEBUG(errs() << ", ");
      else
        DEBUG(errs() << "\n");
    }
  }
  DEBUG(errs() << "------------------------------------------------------\n\n");
}

// for now pick the cheapest remedy for each criticism
// TODO: perform instead global reasoning and consider the best set of
// remedies for a given set of criticisms
void Orchestrator::addressCriticisms(SelectedRemedies &selectedRemedies,
                                     unsigned long &selectedRemediesCost,
                                     Criticisms &criticisms) {
  DEBUG(errs() << "\n-====================================================-\n");
  DEBUG(errs() << "Selected Remedies:\n");
  for (Criticism *cr : criticisms) {
    SetOfRemedies &sors = mapCriticismsToRemeds[cr];
    Remedies_ptr cheapestR = *(sors.begin());
    for (auto &r : *cheapestR) {
      if (!selectedRemedies.count(r)) {
        selectedRemediesCost += r->cost;
        selectedRemedies.insert(r);
      }
    }
    printSelected(sors, cheapestR, *cr);
  }
  DEBUG(errs() << "-====================================================-\n\n");
  printRemediatorSelectionCnt();
  DEBUG(errs() << "\n-====================================================-\n\n");
}

bool Orchestrator::findBestStrategy(
    Loop *loop, llvm::PDG &pdg, LoopDependenceInfo &ldi,
    PerformanceEstimator &perf, ControlSpeculation *ctrlspec,
    PredictionSpeculation *loadedValuePred,
    PredictionSpeculation *headerPhiPred, ModuleLoops &mloops,
    SmtxSlampSpeculationManager &smtxMan, SmtxSpeculationManager &smtxLampMan,
    const Read &rd, const HeapAssignment &asgn, Pass &proxy, LoopAA *loopAA,
    LoopProfLoad &lpl, std::unique_ptr<PipelineStrategy> &strat,
    std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
    unsigned threadBudget, bool ignoreAntiOutput, bool includeReplicableStages,
    bool constrainSubLoops, bool abortIfNoParallelStage) {
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  DEBUG(errs() << "Start of findBestStrategy for loop " << fcn->getName()
               << "::" << header->getName();
        Instruction *term = header->getTerminator();
        if (term) liberty::printInstDebugInfo(term);
        errs() << "\n";);

  /*
  // avoid computing internal pdg to reduce memory consumption
  // create pdg without live-in and live-out values (aka external to the loop nodes)
  std::vector<Value *> iPdgNodes;
  for (auto node : pdg.internalNodePairs()) {
    iPdgNodes.push_back(node.first);
  }
  PDG *ipdg = pdg.createSubgraphFromValues(iPdgNodes, false);
  */

  unsigned long maxSavings = 0;

  // get all possible criticisms
  Criticisms allCriticisms = Critic::getAllCriticisms(pdg);

  // address all possible criticisms
  std::vector<Remediator_ptr> remeds = getRemediators(
      loop, &pdg, ctrlspec, loadedValuePred, headerPhiPred, mloops, ldi,
      smtxMan, smtxLampMan, rd, asgn, proxy, loopAA);
  for (auto remediatorIt = remeds.begin(); remediatorIt != remeds.end();
       ++remediatorIt) {
    Remedies remedies = (*remediatorIt)->satisfy(pdg, loop, allCriticisms);
    for (Remedy_ptr r : remedies) {
      for (Criticism *c : r->resolvedC) {
        // one single remedy resolves this criticism
        Remedies_ptr remedSet = std::make_shared<Remedies>();
        remedSet->insert(r);
        mapCriticismsToRemeds[c].insert(remedSet);
        c->setRemovable(true);
        c->processNewRemovalCost(r->cost);
      }
    }
  }

  #if 0
  // second level remediators, produce both remedies and criticisms
  auto dswpPlusRemediator =
      std::make_unique<DswpPlusRemediator>(loop, &pdg, perf);
  Remediator::RemedCriticResp dswpPlusResp = dswpPlusRemediator->satisfy();
  Remedy_ptr &r = dswpPlusResp.remedy;
  for (Criticism *c : r->resolvedC) {
    // one single remedy resolves this criticism
    Remedies_ptr remedSet = std::make_shared<Remedies>();
    remedSet->insert(r);
    mapCriticismsToRemeds[c].insert(remedSet);
    c->setRemovable(true);
  }

  auto loopFissionRemediator =
      std::make_unique<LoopFissionRemediator>(loop, &pdg, perf);

  std::unordered_set<Criticism*> criticismsResolvedByLoopFission;
  std::map<Criticism*, Remedies_ptr> loopFissionCriticismsToRemeds;

  for (Criticism *c : allCriticisms) {
    Remediator::RemedCriticResp resp = loopFissionRemediator->satisfy(loop, c);
    if (resp.depRes == DepResult::Dep)
      continue;

    // collect all the remedies required to satisfy this criticism
    Remedies_ptr remeds = std::make_shared<Remedies>();
    remeds->insert(resp.remedy);

    // satisfy all criticisms of the loop fission remediator
    // for now select the cheapest one
    // for just coverage purposes avoid taking into account criticisms of loopFission to reduce memory pressure on some SPEC benchmarks
    /*
    for (Criticism *remedC : *resp.criticisms) {
      SetOfRemedies &sors = mapCriticismsToRemeds[remedC];
      Remedies_ptr cheapestR = *(sors.begin());
      assert(
          cheapestR->size() == 1 &&
          "Multiple remedies for one criticism from first-level remediators");
      Remedy_ptr chosenR = *(cheapestR->begin());
      remeds->insert(chosenR);
    }
    */

    criticismsResolvedByLoopFission.insert(c);
    loopFissionCriticismsToRemeds[c] = remeds;
    c->setRemovable(true);
  }

  for (Criticism *c : criticismsResolvedByLoopFission) {
    auto &remeds = loopFissionCriticismsToRemeds[c];
    mapCriticismsToRemeds[c].insert(remeds);
  }
  #endif

  // receive actual criticisms from critics given the enhanced pdg
  std::vector<Critic_ptr> critics = getCritics(&perf, threadBudget, &lpl);
  for (auto criticIt = critics.begin(); criticIt != critics.end(); ++criticIt) {
    DEBUG(errs() << "\nCritic " << (*criticIt)->getCriticName() << "\n");
    CriticRes res = (*criticIt)->getCriticisms(pdg, loop, ldi);
    Criticisms &criticisms = res.criticisms;
    unsigned long expSpeedup = res.expSpeedup;

    if (!expSpeedup) {
      DEBUG(errs() << (*criticIt)->getCriticName()
                   << " not applicable/profitable to " << fcn->getName()
                   << "::" << header->getName()
                   << ": not all criticisms are addressable\n");
      continue;
    }

    std::unique_ptr<SelectedRemedies> selectedRemedies =
        std::unique_ptr<SelectedRemedies>(new SelectedRemedies());
    unsigned long selectedRemediesCost = 0;
    if (!criticisms.size()) {
      DEBUG(errs() << "No criticisms generated\n");
    } else {
      DEBUG(errs() << "Addressible criticisms\n");
      // orchestrator selects set of remedies to address the given criticisms,
      // computes remedies' total cost
      addressCriticisms(*selectedRemedies, selectedRemediesCost, criticisms);
    }

    unsigned long adjRemedCosts =
        (long)Critic::FixedPoint * selectedRemediesCost;
    unsigned long savings = expSpeedup - adjRemedCosts;

    DEBUG(errs() << "Expected Savings from critic "
                 << (*criticIt)->getCriticName()
                 << " (no remedies): " << expSpeedup
                 << "  and selected remedies cost: " << adjRemedCosts << "\n");

    // for coverage purposes, given that the cost model is not complete and not
    // consistent among speedups and remedies cost, assume that it is always
    // profitable to parallelize if loop is DOALL-able.
    //
    //if (maxSavings  < savings) {
    if (!maxSavings || maxSavings < savings) {
      maxSavings = savings;
      strat = std::move(res.ps);
      sRemeds = std::move(selectedRemedies);
      sCritic = *criticIt;
    }
  }

  //delete ipdg;

  if (maxSavings)
    return true;

  // no profitable parallelization strategy was found for this loop
  return false;
}

} // namespace SpecPriv
} // namespace liberty
