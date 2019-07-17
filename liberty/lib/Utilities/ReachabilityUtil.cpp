#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"

#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReachabilityUtil.h"

#include <vector>

using namespace llvm;
using namespace liberty;

bool liberty::noStoreInBetween(const Instruction *firstI,
                               const Instruction *secondI,
                               std::vector<const Instruction *> &defs,
                               ModuleLoops &mloops) {

  if (defs.empty())
    return true;

  // check if all stores, firstI and secondI belong to same function.
  // If so, we can make use of dominator tree and loopInfo to speedup
  // reachabilty query
  bool intraprocedural = firstI->getFunction() == secondI->getFunction();
  if (intraprocedural) {
    for (auto src : defs) {
      intraprocedural &= firstI->getFunction() == src->getFunction();
      if (!intraprocedural)
        break;
    }
  }

  // conservatively report false for interprocedural queries
  if (!intraprocedural)
    return false;

  bool storeInBetween = false;
  DominatorTree *dt =
      (intraprocedural)
          ? &mloops.getAnalysis_DominatorTree(firstI->getFunction())
          : nullptr;
  LoopInfo *tmpLI = (intraprocedural)
                        ? &mloops.getAnalysis_LoopInfo(firstI->getFunction())
                        : nullptr;

  // more conservative check than it should be.
  // even if there is a path from firstI to src, could check if there is at
  // least one path that does not pass through secondI (e.g. using exclusionSet
  // of LLVM 9.0 version of isPotentiallyReachable)
  for (auto src : defs) {
    storeInBetween |= llvm::isPotentiallyReachable(firstI, src, dt, tmpLI);
    if (storeInBetween)
      break;
  }
  return !storeInBetween;
}
