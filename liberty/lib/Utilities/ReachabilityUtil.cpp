#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"

#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReachabilityUtil.h"

#include <queue>
#include <unordered_set>

using namespace llvm;
using namespace liberty;

bool liberty::isReachableIntraprocedural(const Instruction *src,
                                         const Instruction *dst,
                                         ModuleLoops &mloops) {
  if (src->getFunction() != dst->getFunction())
    // not intraprocedural, return conservative answer
    return true;

  DominatorTree *dt = &mloops.getAnalysis_DominatorTree(src->getFunction());
  LoopInfo *li = &mloops.getAnalysis_LoopInfo(dst->getFunction());

  return llvm::isPotentiallyReachable(src, dst, dt, li);
}

bool liberty::isReachableIntraprocedural(RelInstsV &srcInsts,
                                         const Instruction *dst,
                                         ModuleLoops &mloops) {
  for (auto src : srcInsts) {
    if (isReachableIntraprocedural(src, dst, mloops))
      return true;
  }
  return false;
}

Fun2RelInsts liberty::populateFun2RelInsts(const Instruction *I) {
  Fun2RelInsts fun2RelInsts;
  fun2RelInsts[I->getFunction()].push_back(I);
  std::unordered_set<const Function*> visited;
  std::queue<const Function*> worklist;
  worklist.push(I->getFunction());
  while (!worklist.empty()) {
    const Function *F = worklist.front();
    worklist.pop();
    if (visited.count(F))
      continue;
    visited.insert(F);
    if (F->getName() == "main")
     continue;
    for (auto j = F->user_begin(), z = F->user_end(); j != z; ++j) {
      if (const Instruction *inst = dyn_cast<Instruction>(&**j)) {
        fun2RelInsts[inst->getFunction()].push_back(inst);
        worklist.push(inst->getFunction());
      }
    }
  }
  return fun2RelInsts;
}

bool liberty::isReachableInterProcedural(Fun2RelInsts &fun2RelInsts,
                                         const Instruction *dst,
                                         ModuleLoops &mloops) {
  const Function *dstF = dst->getFunction();
  if (fun2RelInsts.count(dstF)) {
    if (isReachableIntraprocedural(fun2RelInsts[dstF], dst, mloops))
      return true;
    return false;
  }
  std::unordered_set<const Function *> visited;
  std::queue<const Function *> worklist;
  worklist.push(dst->getFunction());
  while (!worklist.empty()) {
    const Function *F = worklist.front();
    worklist.pop();
    if (visited.count(F))
      continue;
    visited.insert(F);
    if (F->getName() == "main")
      continue;
    for (auto j = F->user_begin(), z = F->user_end(); j != z; ++j) {
      if (const Instruction *inst = dyn_cast<Instruction>(&**j)) {

        const Function *userF = inst->getFunction();
        if (fun2RelInsts.count(userF)) {
          if (isReachableIntraprocedural(fun2RelInsts[userF], inst, mloops))
            return true;
          continue;
        }
        worklist.push(inst->getFunction());
      }
    }
  }
  return false;
}

bool liberty::isReachableInterProcedural(const Instruction *src,
                                         std::vector<const Instruction *> dsts,
                                         ModuleLoops &mloops) {
  Fun2RelInsts fun2RelInsts = populateFun2RelInsts(src);
  for (auto dst : dsts) {
    if (isReachableInterProcedural(fun2RelInsts, dst, mloops))
      return true;
  }
  return false;
}

bool liberty::noStoreInBetween(const Instruction *firstI,
                               const Instruction *secondI,
                               std::vector<const Instruction *> &defs,
                               ModuleLoops &mloops) {

  // more conservative check than it should be.
  // even if there is a path from firstI to src, could check if there is at
  // least one path that does not pass through secondI (e.g. using exclusionSet
  // of LLVM 9.0 version of isPotentiallyReachable)
  return !isReachableInterProcedural(firstI, defs, mloops);
}
