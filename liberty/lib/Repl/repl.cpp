#include "llvm/Pass.h"
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
  while (true) {
    string query;
    cin >> query;
    // clean up query

    if (query == "loops") {
      ModuleLoops &mloops = getAnalysis<ModuleLoops>();
      const Targets &targets = getAnalysis<Targets>();

      for (Targets::iterator i = targets.begin(mloops),
                             e = targets.end(mloops);
           i != e; ++i) {
        Loop *loop = *i;
        outs() << loop->getHeader()->getParent()->getName()
               << "::" << loop->getHeader()->getName() << '\n';
        continue;
      }
    }
    if (query == "quit") {
      break;
    }

  }

  return modified;
}
