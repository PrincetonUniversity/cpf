// Aggressive Inliner
//
// Inlines functions in call sites of target hot loops

#define DEBUG_TYPE "inliner"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/Utilities/ModuleLoops.h"

#include <queue>
#include <unordered_set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numInlinedCallSites,     "Num of inlined call sites");

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
  // keep track of valid for inlining, already processed, call insts
  std::unordered_set<CallInst *> validCallInsts;

  // keep all the call insts that need to be inlined, ordered based on call
  // graph depth
  std::queue<CallInst *> inlineCallInsts;

  // keep list of already processed functions
  std::unordered_set<Function*> processedFunctions;

  void processBB(BasicBlock &BB,
                 std::unordered_set<Function *> &curPathVisited) {
    for (Instruction &I : BB) {
      if (CallInst *call = dyn_cast<CallInst>(&I)) {
        Function *calledFun = call->getCalledFunction();
        if (calledFun && !calledFun->isDeclaration()) {
          if (validCallInsts.count(call))
            continue;
          if (processFunction(calledFun, curPathVisited)) {
            inlineCallInsts.push(call);
            validCallInsts.insert(call);
          }
        }
      }
    }
  }

  bool processFunction(Function *F,
                       std::unordered_set<Function *> &curPathVisited) {
    // check if there is a cycle in call graph
    if (curPathVisited.count(F))
      return false;
    curPathVisited.insert(F);

    if (processedFunctions.count(F)) {
      curPathVisited.erase(F);
      return true;
    }
    processedFunctions.insert(F);

    for (BasicBlock &BB : *F)
      processBB(BB, curPathVisited);

    curPathVisited.erase(F);
    return true;
  }

  void runOnTargetLoop(Loop *loop) {
    std::unordered_set<Function *> curPathVisited;
    Function *loopFun = loop->getHeader()->getParent();
    curPathVisited.insert(loopFun);

    for (BasicBlock *BB : loop->getBlocks())
      processBB(*BB, curPathVisited);
  }

  bool transform() {
    bool modified = false;

    ModuleLoops &mloops = getAnalysis<ModuleLoops>();
    const Targets &targets = getAnalysis<Targets>();
    for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
         i != e; ++i) {
      runOnTargetLoop(*i);
    }

    // performing function inlining on collected call insts
    while (!inlineCallInsts.empty()) {
      auto callInst = inlineCallInsts.front();
      inlineCallInsts.pop();
      InlineFunctionInfo IFI;
      modified |= InlineFunction(CallSite(callInst), IFI);
      ++numInlinedCallSites;
    }

    return modified;
  }
};

char Inliner::ID = 0;
static RegisterPass<Inliner> mpp("aggr-inliner",
                                 "Aggressive inlining in hot loops");

} // namespace SpecPriv
} // namespace liberty
