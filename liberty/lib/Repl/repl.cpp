#include "DGBase.hpp"
#include "PDG.hpp"
#include "SCCDAG.hpp"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/Orchestrator.h"
#include "liberty/Speculation/PDGBuilder.hpp"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "liberty/Utilities/ReportDump.h"
#include "liberty/Orchestration/PSDSWPCritic.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

using namespace llvm;
using namespace std;
using namespace liberty;
using namespace llvm::noelle;

namespace {
class OptRepl : public ModulePass {
public:
  static char ID;
  void getAnalysisUsage(AnalysisUsage &au) const;
  StringRef getPassName() const { return "remed-selector"; }
  bool runOnModule(Module &M);
  OptRepl() : ModulePass(ID) {}
};
} // namespace

char OptRepl::ID = 0;
static RegisterPass<OptRepl> rp("opt-repl", "Opt Repl");

void OptRepl::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<ModuleLoops>();
  au.addRequired<Targets>();
  au.addRequired<PDGBuilder>();
  au.addRequired< LoopProfLoad >();
  au.addRequired< ProfilePerformanceEstimator >();
  au.setPreservesAll();
  // au.addRequired< SmtxSpeculationManager >();
  // au.addRequired< PtrResidueSpeculationManager >();
  // au.addRequired< ProfileGuidedControlSpeculator >();
  // au.addRequired< ProfileGuidedPredictionSpeculator >();
  // au.addRequired<LoopAA>();
  // au.addRequired<ReadPass>();
  // au.addRequired<Classify>();
  // au.addRequired<KillFlow_CtrlSpecAware>();
  // au.addRequired<CallsiteDepthCombinator_CtrlSpecAware>();
  // au.addRequired<CallGraphWrapperPass>();
}
typedef map<unsigned, DGNode<Value> *> InstIdMap_t;
typedef map<DGNode<Value> *, unsigned> InstIdReverseMap_t;
typedef map<unsigned, DGEdge<Value> *> DepIdMap_t;

static unique_ptr<InstIdMap_t> createInstIdMap(PDG *pdg) {
  auto instIdMap = std::make_unique<InstIdMap_t>();
  unsigned instId = 0;
  for (auto &instNode : pdg->getNodes()) {
    instIdMap->insert(make_pair(instId, instNode));
    instId++;
  }

  return instIdMap;
}

static unique_ptr<InstIdReverseMap_t> createInstIdLookupMap(InstIdMap_t m) {
  auto lookupMap = std::make_unique<InstIdReverseMap_t>();
  for (auto &[instId, node] : m) {
    lookupMap->insert(make_pair(node, instId));
  }

  return lookupMap;
}

static unsigned getNumberFromQuery(string &query) {
  // keep only numbers
  query.erase(remove_if(query.begin(), query.end(),
                        [](unsigned char a) { return !std::isdigit(a); }),
              query.end());
  if (query.size() == 0) {
    return -1;
  }

  return stoi(query);
}

bool OptRepl::runOnModule(Module &M) {
  bool modified = false;
  Loop *selectedLoop;
  Function *selectedFunction;

  // store the loopID
  map<unsigned, Loop *> loopIdMap;
  ModuleLoops &mloops = getAnalysis<ModuleLoops>();
  const Targets &targets = getAnalysis<Targets>();
  PDGBuilder &pdgbuilder = getAnalysis<PDGBuilder>();
  unique_ptr<PDG> selectedPDG;
  unique_ptr<SCCDAG> selectedSCCDAG;

  unique_ptr<InstIdMap_t> instIdMap;
  unique_ptr<InstIdReverseMap_t> instIdLookupMap;
  unique_ptr<DepIdMap_t> depIdMap;

  // prepare hot loops
  {
    unsigned loopId = 0;
    for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
         i != e; ++i) {
      Loop *loop = *i;
      loopIdMap[loopId++] = loop;
      continue;
    }
  }

  while (true) {
    outs() << "(opt-repl) ";
    string query;
    getline(cin, query);

    if (query == "loops") {
      outs() << "List of hot loops:\n";

      for (auto &[loopId, loop] : loopIdMap) {
        outs() << loopId << ": " << loop->getHeader()->getParent()->getName()
               << "::" << loop->getHeader()->getName() << '\n';
      }
      continue;
    }

    if (query.find("select") == 0 || query.find("s ") == 0) {
      unsigned loopId = getNumberFromQuery(query);
      if (loopId == -1) {
        outs() << "No number specified\n";
        continue;
      }

      if (loopIdMap.find(loopId) == loopIdMap.end()) {
        outs() << "Loop " << loopId << " does not exist\n";
        continue;
      }

      Loop *loop = loopIdMap[loopId];
      outs() << "Selecting loop " << loopId << ": ";
      outs() << loop->getHeader()->getParent()->getName()
             << "::" << loop->getHeader()->getName() << '\n';
      selectedLoop = loop;

      selectedPDG = pdgbuilder.getLoopPDG(loop);
      selectedSCCDAG = std::make_unique<SCCDAG>(selectedPDG.get());

      instIdMap = createInstIdMap(selectedPDG.get());
      instIdLookupMap = createInstIdLookupMap(*instIdMap);

      continue;
    }

    if (query == "quit") {
      break;
    }

    if (!selectedLoop) {
      outs() << "No loops selected\n";
      continue;
    }
    if (query.find("dump") == 0) {
      outs() << *selectedLoop;
      outs() << "Number of instructions: "
             << selectedPDG->getNumberOfInstructionsIncluded() << "\n";
      outs() << "Number of dependences: "
             << selectedPDG->getNumberOfDependencesBetweenInstructions()
             << "\n";
      outs() << "Number of SCCs: " << selectedSCCDAG->numNodes();

      outs() << "\n";

      if (query.find("-v") != string::npos) {
        for (auto block : selectedLoop->getBlocks()) {
          outs() << *block;
        }
      }
      outs() << "\n";
      continue;
    }

    if (query.find("insts") == 0) {
      for (auto &[instId, node] : *instIdMap) {
        outs() << instId << "\t" << *node->getT() << "\n";
      }
      continue;
    }

    auto dumpEdge = [&instIdLookupMap](unsigned depId, DGEdge<Value> *edge) {
      auto idA = instIdLookupMap->at(edge->getOutgoingNode());
      auto idB = instIdLookupMap->at(edge->getIncomingNode());
      outs() << depId << "\t" << idA << "->" << idB << ":\t" << edge->toString() << (edge->isLoopCarriedDependence() ? "(LC)" : "(LL)")
             << "\n";
    };

    if (query == "deps") {
      depIdMap = std::make_unique<DepIdMap_t>();
      unsigned id = 0;
      for (auto &edge : selectedPDG->getEdges()) {
        dumpEdge(id, edge);
        depIdMap->insert(make_pair(id++, edge));
      }
      continue;
    }

    if (query.find("deps from") == 0) {
      unsigned instId = getNumberFromQuery(query);
      if (instId == -1) {
        outs() << "No number specified\n";
        continue;
      }

      if (instIdMap->find(instId) == instIdMap->end()) {
        outs() << "InstId " << instId << " not found\n";
        continue;
      }

      auto node = instIdMap->at(instId);
      depIdMap = std::make_unique<DepIdMap_t>();
      unsigned id = 0;
      for (auto &edge : node->getOutgoingEdges()) {
        dumpEdge(id, edge);
        depIdMap->insert(make_pair(id++, edge));
      }
      continue;
    }

    if (query.find("deps to") == 0) {
      unsigned instId = getNumberFromQuery(query);
      if (instId == -1) {
        outs() << "No number specified\n";
        continue;
      }

      if (instIdMap->find(instId) == instIdMap->end()) {
        outs() << "InstId " << instId << " not found\n";
        continue;
      }

      auto node = instIdMap->at(instId);
      depIdMap = std::make_unique<DepIdMap_t>();
      unsigned id = 0;
      for (auto &edge : node->getIncomingEdges()) {
        dumpEdge(id, edge);
        depIdMap->insert(make_pair(id++, edge));
      }
      continue;
    }

    // remove a dependence
    if (query.find("remove") == 0) {
      unsigned depId = getNumberFromQuery(query);
      if (depId == -1) {
        outs() << "No number specified\n";
        continue;
      }

      if (depIdMap->find(depId) == depIdMap->end()) {
        outs() << "DepId" << depId << " not found\n";
        continue;
      }

      auto dep = depIdMap->at(depId);
      selectedPDG->removeEdge(dep);
      // update SCCDAG
      selectedSCCDAG = std::make_unique<SCCDAG>(selectedPDG.get());

      continue;
    }

    // try to parallelize
    if (query.find("para") == 0) {
      // initialize performance estimator
      unsigned threadBudget = getNumberFromQuery(query);
      if (threadBudget == -1) {
        threadBudget = 28;
      }

      LoopProfLoad *lpl = &getAnalysis<LoopProfLoad>();
      auto perf = &getAnalysis<ProfilePerformanceEstimator>();

      auto psdswp = std::make_shared<PSDSWPCritic>(perf, threadBudget, lpl);
      auto doall = std::make_shared<DOALLCritic>(perf, threadBudget, lpl);

      auto check = [](Critic_ptr critic, string name, PDG &pdg, Loop* loop) {
        CriticRes res = critic->getCriticisms(pdg, loop);
        Criticisms &criticisms = res.criticisms;
        unsigned long expSaving = res.expSpeedup;

        if (!expSaving) {
          outs() << name << " not applicable/profitable\n";
        } else {
          outs() << name << " applicable, estimated savings: " << expSaving << "\n";
        }
      };

      check(doall, "DOALL", *selectedPDG.get(), selectedLoop);
      check(psdswp, "PSDSWPCritic", *selectedPDG.get(), selectedLoop);

      continue;
    }

    // modref
    if (query.find("modref") == 0) {

      LoopAA *aa = getAnalysis< LoopAA >().getTopAA();

    }
  }

  return modified;
}
