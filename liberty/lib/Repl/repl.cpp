#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/Orchestrator.h"
#include "liberty/Orchestration/PSDSWPCritic.h"
#include "liberty/Speculation/PDGBuilder.hpp"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "liberty/Utilities/ReportDump.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <iostream>
// GNU Readline
#include <readline/history.h>
#include <readline/readline.h>
#include <string>

#include "noelle/core/DGBase.hpp"
#include "noelle/core/PDG.hpp"
#include "noelle/core/PDGPrinter.hpp"
#include "noelle/core/SCCDAG.hpp"
#include "ReplParse.hpp"

using namespace llvm;
using namespace std;
using namespace liberty;
using namespace llvm::noelle;

class OptRepl : public ModulePass {
  public:
    static char ID;
    void getAnalysisUsage(AnalysisUsage &au) const;
    StringRef getPassName() const { return "remed-selector"; }
    bool runOnModule(Module &M);
    OptRepl() : ModulePass(ID) {}
};

char OptRepl::ID = 0;
static RegisterPass<OptRepl> rp("opt-repl", "Opt Repl");

void OptRepl::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<ModuleLoops>();
  au.addRequired<Targets>();
  au.addRequired<PDGBuilder>();
  au.addRequired< LoopProfLoad >();
  au.addRequired< ProfilePerformanceEstimator >();
  au.addRequired<LoopAA>();
  au.setPreservesAll();
}

typedef map<unsigned, DGNode<Value> *> InstIdMap_t;
typedef map<DGNode<Value> *, unsigned> InstIdReverseMap_t;
typedef map<unsigned, DGEdge<Value> *> DepIdMap_t;
typedef map<DGEdge<Value> *, unsigned> DepIdReverseMap_t;

// helper function to generate
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

static shared_ptr<DepIdReverseMap_t> createDepIdLookupMap(DepIdMap_t m) {
  auto lookupMap = std::make_shared<DepIdReverseMap_t>();
  for (auto &[instId, node] : m) {
    lookupMap->insert(make_pair(node, instId));
  }

  return lookupMap;
}

// a simple autocompletion generator
char* completion_generator(const char* text, int state) {
  // This function is called with state=0 the first time; subsequent calls are
  // with a nonzero state. state=0 can be used to perform one-time
  // initialization for this completion session.
  static std::vector<std::string> matches;
  static size_t match_index = 0;

  if (state == 0) {
    // During initialization, compute the actual matches for 'text' and keep
    // them in a static vector.
    matches.clear();
    match_index = 0;

    // Collect a vector of matches: vocabulary words that begin with text.
    std::string textstr = std::string(text);
    for (auto word : ReplVocab) {
      if (word.size() >= textstr.size() &&
          word.compare(0, textstr.size(), textstr) == 0) {
        matches.push_back(word);
      }
    }
  }

  if (match_index >= matches.size()) {
    // We return nullptr to notify the caller no more matches are available.
    return nullptr;
  } else {
    // Return a malloc'd char* for the match. The caller frees it.
    return strdup(matches[match_index++].c_str());
  }
}

char** completer(const char* text, int start, int end) {
  // Don't do filename completion even if our generator finds no matches.
  rl_attempted_completion_over = 1;

  // Note: returning nullptr here will make readline use the default filename
  // completer.
  return rl_completion_matches(text, completion_generator);
}


bool OptRepl::runOnModule(Module &M) {
  bool modified = false;

  ModuleLoops &mloops = getAnalysis<ModuleLoops>();
  const Targets &targets = getAnalysis<Targets>();
  PDGBuilder &pdgbuilder = getAnalysis<PDGBuilder>();

  // store the loopID
  map<unsigned, Loop *> loopIdMap;

  // the selected information
  llvm::Function *selectedFunction; // TODO: not used yet
  Loop *selectedLoop;
  unique_ptr<PDG> selectedPDG;
  unique_ptr<SCCDAG> selectedSCCDAG;

  unique_ptr<InstIdMap_t> instIdMap;
  unique_ptr<InstIdReverseMap_t> instIdLookupMap;
  unique_ptr<DepIdMap_t> depIdMap;
  shared_ptr<DepIdReverseMap_t> depIdLookupMap;

  // prepare hot loops from the targets
  {
    unsigned loopId = 0;
    for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
         i != e; ++i) {
      Loop *loop = *i;
      loopIdMap[loopId++] = loop;
      continue;
    }
  }

  rl_attempted_completion_function = completer;
  // the main repl while loop
  while (true) {
    string query;
    char *buf = readline("(opt-repl) ");
    query = (const char *)(buf);
    if (query.size() > 0) {
      add_history(buf);
      free(buf); // free the buf readline created
    }

    // check if it's quit or unknown
    ReplParser parser(query);
    if (parser.getAction() == ReplAction::Quit)
      break;

    if (parser.getAction() == ReplAction::Unknown) {
      outs() << "Unknown command!\n";
      continue;
    }

    // print all loops
    auto loopsFn = [&loopIdMap]() {
      outs() << "List of hot loops:\n";

      for (auto &[loopId, loop] : loopIdMap) {
        outs() << loopId << ": " << loop->getHeader()->getParent()->getName()
               << "::" << loop->getHeader()->getName() << '\n';
      }
    };

    // select one loop
    auto selectFn = [&loopIdMap, &parser, &selectedLoop, &selectedPDG, &selectedSCCDAG, &pdgbuilder, &instIdMap, &instIdLookupMap]() {
      int loopId = parser.getActionId();
      if (loopId == -1) {
        outs() << "No number specified\n";
        return;
      }

      if (loopIdMap.find(loopId) == loopIdMap.end()) {
        outs() << "Loop " << loopId << " does not exist\n";
        return;
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
    };

    // show help
    auto helpFn = [&parser]() {
      string action = parser.getStringAfterAction();
      if (ReplActions.find(action) != ReplActions.end()) {
        outs() << HelpText.at(ReplActions.at(action)) << "\n";
      }
      else {
        for (auto &[action, explaination] : HelpText) {
          outs() << explaination << "\n";
        }
      }
    };

    // early checks for several actions that do not need the loop set
    if (parser.getAction() == ReplAction::Loops) {
      loopsFn();
      continue;
    }

    if (parser.getAction() == ReplAction::Select) {
      selectFn();
      continue;
    }

    if (parser.getAction() == ReplAction::Help){
      helpFn();
      continue;
    }

    // after this assume the loop has been selected
    if (!selectedLoop) {
      outs() << "No loops selected\n";
      continue;
    }

    // dump information about the loop
    auto dumpFn = [&parser, &selectedLoop, &selectedPDG, &selectedSCCDAG]() {
      outs() << *selectedLoop;
      outs() << "Number of instructions: "
             << selectedPDG->getNumberOfInstructionsIncluded() << "\n";
      outs() << "Number of dependences: "
             << selectedPDG->getNumberOfDependencesBetweenInstructions()
             << "\n";
      outs() << "Number of SCCs: " << selectedSCCDAG->numNodes();

      outs() << "\n";

      if (parser.isVerbose()) {
        for (auto block : selectedLoop->getBlocks()) {
          outs() << *block;
        }
      }
      outs() << "\n";
    };

    // show instructions with id
    auto InstsFn = [&instIdMap]() {
      for (auto &[instId, node] : *instIdMap) {
        outs() << instId << "\t" << *node->getT() << "\n";
      }
    };

    // helper function for dumping edge
    auto dumpEdge = [&instIdLookupMap](unsigned depId, DGEdge<Value> *edge) {
      auto idA = instIdLookupMap->at(edge->getOutgoingNode());
      auto idB = instIdLookupMap->at(edge->getIncomingNode());
      outs() << depId << "\t" << idA << "->" << idB << ":\t" << edge->toString() << (edge->isLoopCarriedDependence() ? "(LC)" : "(LL)")
             << "\n";
    };

    // show all deps with id; also generate a currentPDG.dot file, the edge number is annotated on the PDG
    auto depsFn = [&instIdLookupMap, &parser, &depIdMap, &depIdLookupMap, &selectedPDG, &dumpEdge, &instIdMap]() {
      int fromId = parser.getFromId();
      int toId = parser.getToId();
      if (fromId != -1) {
        if (instIdMap->find(fromId) == instIdMap->end()) {
          outs() << "From InstId " << fromId<< " not found\n";
          return;
        }
      }

      if (toId != -1) {
        if (instIdMap->find(toId) == instIdMap->end()) {
          outs() << "To InstId " << toId<< " not found\n";
          return;
        }
      }

      depIdMap = std::make_unique<DepIdMap_t>();
      unsigned id = 0;
      if (fromId == -1 && toId == -1) { // both not specified
        for (auto &edge : selectedPDG->getEdges()) {
          dumpEdge(id, edge);
          depIdMap->insert(make_pair(id++, edge));
        }
      } else if (fromId != -1 && toId != -1) { // both specified
        auto fromNode = instIdMap->at(fromId);
        auto toNode = instIdMap->at(toId);
        for (auto &edge : fromNode->getOutgoingEdges()) {
          if (edge->getIncomingNode() == toNode) {
            dumpEdge(id, edge);
            depIdMap->insert(make_pair(id++, edge));
          }
        }
      } else if (fromId != -1) { // from is specified
        auto node = instIdMap->at(fromId);
        for (auto &edge : node->getOutgoingEdges()) {
          dumpEdge(id, edge);
          depIdMap->insert(make_pair(id++, edge));
        }
      } else if (toId != -1) { // to is specified
        auto node = instIdMap->at(toId);
        for (auto &edge : node->getIncomingEdges()) {
          dumpEdge(id, edge);
          depIdMap->insert(make_pair(id++, edge));
        }
      }

      depIdLookupMap = createDepIdLookupMap(*depIdMap);
      selectedPDG->setDepLookupMap(depIdLookupMap);
      llvm::noelle::DGPrinter::writeClusteredGraph<PDG, Value>("currentPDG.dot", selectedPDG.get());
    };

    // remove a dependence
    auto removeFn = [&parser, &depIdMap, &selectedPDG, &selectedSCCDAG]() {
      int depId = parser.getActionId();
      if (depId == -1) {
        outs() << "No number specified\n";
        return;
      }

      if (depIdMap->find(depId) == depIdMap->end()) {
        outs() << "DepId" << depId << " not found\n";
        return;
      }

      auto dep = depIdMap->at(depId);
      selectedPDG->removeEdge(dep);
      // update SCCDAG
      selectedSCCDAG = std::make_unique<SCCDAG>(selectedPDG.get());
    };

    // try to parallelize
    auto parallelizeFn = [&parser, this, &selectedPDG, &selectedLoop]() {
      int threadBudget = parser.getActionId();
      if (threadBudget == -1) {
        threadBudget = 28;
      }

      LoopProfLoad *lpl = &getAnalysis<LoopProfLoad>();
      auto perf = &getAnalysis<ProfilePerformanceEstimator>();

      // initialize performance estimator
      auto psdswp = std::make_shared<PSDSWPCritic>(perf, threadBudget, lpl);
      auto doall = std::make_shared<DOALLCritic>(perf, threadBudget, lpl);

      auto check = [](Critic_ptr critic, string name, PDG &pdg, Loop *loop) {
        CriticRes res = critic->getCriticisms(pdg, loop);
        Criticisms &criticisms = res.criticisms;
        unsigned long expSaving = res.expSpeedup;

        if (!expSaving) {
          outs() << name << " not applicable/profitable\n";
        } else {
          outs() << name << " applicable, estimated savings: " << expSaving
                 << "\n";
        }
      };

      check(doall, "DOALL", *selectedPDG.get(), selectedLoop);
      check(psdswp, "PSDSWPCritic", *selectedPDG.get(), selectedLoop);
    };

    // modref: create a modref query and (optionally explore the loopaa stack)
    auto modrefFn = [this, &parser, &instIdMap, &selectedLoop]() {
      int fromId = parser.getFromId();
      int toId = parser.getToId();

      if (fromId == -1) {
          outs() << "From InstId not set\n";
          return;
      }
      else {
        if (instIdMap->find(fromId) == instIdMap->end()) {
          outs() << "From InstId " << fromId<< " not found\n";
          return;
        }
      }

      if (toId == -1) {
          outs() << "To InstId not set\n";
          return;
      }
      else {
        if (instIdMap->find(toId) == instIdMap->end()) {
          outs() << "To InstId " << toId<< " not found\n";
          return;
        }
      }
      auto fromInst = dyn_cast<Instruction>(instIdMap->at(fromId)->getT());
      auto toInst = dyn_cast<Instruction>(instIdMap->at(toId)->getT());

      // TODO: one of the instruction can be a value
      if (!fromInst || !toInst) {
        outs() << "Instructions not found\n";
        return;
      }

      LoopAA *aa = getAnalysis<LoopAA>().getTopAA();
      Remedies remeds;

      if (parser.isVerbose()) {
        // TODO: try all combination of analysis and find a setting that the result is different
      }
      else {
        auto ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::Same, toInst, selectedLoop, remeds);
        outs() << *fromInst << "->" << *toInst << ": (Same)" << ret << "\n";
        ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::Before, toInst, selectedLoop, remeds);
        outs() << *fromInst << "->" << *toInst << ": (Before)" << ret << "\n";
        ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::After, toInst, selectedLoop, remeds);
        outs() << *fromInst << "->" << *toInst << ": (After)" << ret << "\n";
      }
    };

    switch (parser.getAction()) {
    case ReplAction::Deps:
      depsFn();
      break;
    case ReplAction::Dump:
      dumpFn();
      break;
    case ReplAction::Insts:
      InstsFn();
      break;
    case ReplAction::Remove:
      removeFn();
      break;
    case ReplAction::Parallelize:
      parallelizeFn();
      break;
    case ReplAction::Modref:
      modrefFn();
      break;
    default:
      outs() << "SHOULD NOT HAPPEN\n";
      break;
    }
  }

  return modified;
}
