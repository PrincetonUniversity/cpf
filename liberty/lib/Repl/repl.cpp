#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "liberty/Utilities/ReportDump.h"
#include "liberty/LoopProf/Targets.h"
#include <cctype>
#include <iostream>
#include <string>
#include <algorithm>

using namespace llvm;
using namespace std;
using namespace liberty;

namespace {
  class OptRepl : public ModulePass {
    public:
      static char ID;
      void getAnalysisUsage(AnalysisUsage &au) const;
      StringRef getPassName() const { return "remed-selector"; }
      bool runOnModule(Module &M);
      OptRepl(): ModulePass(ID){}
  };
}

char OptRepl::ID = 0;
static RegisterPass< OptRepl > rp("opt-repl", "Opt Repl");

void OptRepl::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< ModuleLoops >();
  au.addRequired< Targets >();
  au.setPreservesAll();
  //au.addRequired< SmtxSpeculationManager >();
  //au.addRequired< PtrResidueSpeculationManager >();
  //au.addRequired< ProfileGuidedControlSpeculator >();
  //au.addRequired< ProfileGuidedPredictionSpeculator >();
  //au.addRequired<LoopAA>();
  //au.addRequired<ReadPass>();
  //au.addRequired<Classify>();
  //au.addRequired<KillFlow_CtrlSpecAware>();
  //au.addRequired<CallsiteDepthCombinator_CtrlSpecAware>();
  //au.addRequired<CallGraphWrapperPass>();
}

bool OptRepl::runOnModule(Module &M) {
  bool modified = false;
  Loop *selectedLoop;
  Function *selectedFunction;
  map<unsigned, Loop *> loopIdMap;
  while (true) {
    outs() << "(opt-repl) ";
    string query;
    getline(cin, query);
    // clean up query

    if (query == "loops") {
      outs() << "List of hot loops:\n";
      ModuleLoops &mloops = getAnalysis<ModuleLoops>();
      const Targets &targets = getAnalysis<Targets>();

      unsigned loopId = 0;
      for (Targets::iterator i = targets.begin(mloops),
                             e = targets.end(mloops);
           i != e; ++i) {
        Loop *loop = *i;
        loopIdMap[loopId] = loop;
        outs() << loopId << ": " << loop->getHeader()->getParent()->getName()
               << "::" << loop->getHeader()->getName() << '\n';
        loopId++;
        continue;
      }
    }
    if (query.find("select") == 0) {
      // keep only numbers
      outs() << query << "\n";
      query.erase(remove_if(query.begin(), query.end(),
                            [](unsigned char a) { return !std::isdigit(a); }), query.end());

      unsigned loopId = stoi(query);
      if (loopIdMap.find(loopId) == loopIdMap.end()) {
        outs() << "Loop " << loopId << " does not exist\n";
        continue;
      }
      Loop *loop = loopIdMap[loopId];
      outs() << "Selecting loop " << loopId << ": ";
      outs() << loop->getHeader()->getParent()->getName()
             << "::" << loop->getHeader()->getName() << '\n';
      selectedLoop = loop;
    }
    if (query == "dump") {
      outs() << *selectedLoop;
      selectedLoop->dump();
    }
    if (query == "quit") {
      break;
    }

  }

  return modified;
}
