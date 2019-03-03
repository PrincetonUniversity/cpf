// Aggressive Inliner
//
// Adds attribute "always_inline" to every function in the call graph of hot
// loops

#define DEBUG_TYPE "inliner"

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/Utilities/ModuleLoops.h"

#include <queue>
#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numInlinedFuncs,     "Num of inlined functions");

struct Inliner: public ModulePass
{
  static char ID;
  Inliner() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< ModuleLoops >();
    au.addRequired< Targets >();
  }

  bool runOnModule(Module &mod)
  {
    const bool modified = transform();
    return modified;
  };

private:
  void processBB(BasicBlock &BB, std::queue<Function *> &funQ) {
    for (Instruction &I : BB) {
      if (CallInst *call = dyn_cast<CallInst>(&I)) {
        Function *calledFun = call->getCalledFunction();
        if (calledFun && !calledFun->isDeclaration())
          funQ.push(calledFun);
      }
    }
  }

  void inlineFunctions(std::queue<Function *> &funQ,
                       std::set<Function *> &visited) {
    while (!funQ.empty()) {
      Function *F = funQ.front();
      funQ.pop();
      if (visited.count(F))
        continue;
      visited.insert(F);
      F->addFnAttr(Attribute::AlwaysInline);
      F->removeFnAttr(Attribute::NoInline);
      ++numInlinedFuncs;

      // process function and add more called functions in the worklist
      for (BasicBlock &BB : *F)
        processBB(BB, funQ);
    }
  }

  bool runOnLoop(Loop *loop) {
    bool modified = false;

    std::queue<Function *> funQ;
    std::set<Function *> visited;
    for (BasicBlock *BB : loop->getBlocks())
      processBB(*BB, funQ);

    if (!funQ.empty())
      modified = true;

    inlineFunctions(funQ, visited);

    return modified;
  }

  bool transform() {
    bool modified = false;

    ModuleLoops &mloops = getAnalysis<ModuleLoops>();
    const Targets &targets = getAnalysis<Targets>();
    for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
         i != e; ++i) {
      bool transformed = runOnLoop(*i);
      modified |= transformed;
    }
    return modified;
  }
};

char Inliner::ID = 0;
static RegisterPass<Inliner> mpp(
  "aggr-inliner", "Aggressive inlining in hot loops");

}
}
