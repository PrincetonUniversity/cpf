// Aggressive Inliner
//
// Inlines functions in call sites of target hot loops

#define DEBUG_TYPE "inliner"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Analysis/LoopAA.h"
//#include "liberty/Analysis/LLVMAAResults.h"

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
    au.addRequired< LoopAA >();
    au.addRequired< BlockFrequencyInfoWrapperPass >();
    au.addRequired< Targets >();
//    au.addRequired< LLVMAAResults >();
 //   au.addRequired< PDGBuilder >();
  }

  bool runOnModule(Module &mod)
  {
    const bool modified = transform(mod);
    return modified;
  };

private:
  typedef Module::global_iterator GlobalIt;
  typedef Value::user_iterator UseIt;

  // keep track of valid for inlining, already processed, call insts
  std::unordered_set<CallInst *> validCallInsts;

  // keep all the call insts that need to be inlined, ordered based on call
  // graph depth
  std::queue<CallInst *> inlineCallInsts;

  // keep list of already processed functions
  std::unordered_set<Function*> processedFunctions;

  // avoid inlining functions that are commonly commutative and can be handled
  // as a function call
  bool shouldNotBeInlined(Function *F) {
    std::string random_func_str = "random";
    //std::string malloc_func_str = "alloc";
    std::string funcName = F->getName().str();
    if (funcName.find(random_func_str) != std::string::npos) {
    //    funcName.find(malloc_func_str) != std::string::npos) {
      return true;
    }
    return false;
  }

  bool isRecursiveFnFast(Function *F) {
    for (BasicBlock &BB : *F) {
      for (Instruction &I : BB) {
        if (CallInst *call = dyn_cast<CallInst>(&I)) {
          Function *calledFun = call->getCalledFunction();
          if (calledFun && !calledFun->isDeclaration()) {
            if (calledFun == F)
              return true;
          }
        }
      }
    }
    return false;
  }

  bool isSpeculativelyDeadBB(BasicBlock &BB) {
    Function *fcn = BB.getParent();
    BlockFrequencyInfo &bfi =
        getAnalysis<BlockFrequencyInfoWrapperPass>(*fcn).getBFI();
    if (bfi.getBlockProfileCount(&BB).hasValue()) {
      uint64_t bb_cnt = bfi.getBlockProfileCount(&BB).getValue();
      if (bb_cnt == 0)
        return true;
    }
    return false;
  }

  void processBB(BasicBlock &BB,
                 std::unordered_set<Function *> &curPathVisited) {
    if (isSpeculativelyDeadBB(BB))
      return;
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
    // or if the function is recursive (quick check)
    if (curPathVisited.count(F) || isRecursiveFnFast(F))
      return false;
    curPathVisited.insert(F);

//    if (shouldNotBeInlined(F))
//      return false;

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

    //getAnalysis<LLVMAAResults>().computeAAResults(loop->getHeader()->getParent());
    //LoopAA *aa = getAnalysis< LoopAA >().getTopAA();

    for (BasicBlock *BB : loop->getBlocks())
      processBB(*BB, curPathVisited);
  }

  void inlineCall(CallInst *call) {
    Function *calledFun = call->getCalledFunction();
    if (calledFun && !calledFun->isDeclaration()) {
      if (validCallInsts.count(call))
        return;
      inlineCallInsts.push(call);
      validCallInsts.insert(call);
    }
  }

  // TODO: could improve the inlining for globals by ensuring that all the uses
  // by function calls of each global (including within the whole call graph)
  // can be eliminated (namely when no recursive function needs to be inlined).
  // Should do no inlining unless all captures by fun calls can be removed for a
  // particular global.
  void runOnGlobal(GlobalVariable &global) {
    Type *type = global.getType()->getElementType();
    if (type->isPointerTy()) {
      for (UseIt use = global.user_begin(); use != global.user_end(); ++use) {
        if (CallInst *call = dyn_cast<CallInst>(*use)) {
          inlineCall(call);
        } else if (BitCastOperator *bcOp =
                       dyn_cast<BitCastOperator>(*use)) {
          for (UseIt bUse = bcOp->user_begin(); bUse != bcOp->user_end();
               ++bUse) {
            if (CallInst *bCall = dyn_cast<CallInst>(*bUse)) {
              inlineCall(bCall);
            }
          }
        }
      }
    }
  }

  bool transform(Module &mod) {
    bool modified = false;

    ModuleLoops &mloops = getAnalysis<ModuleLoops>();
    const Targets &targets = getAnalysis<Targets>();
    for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
         i != e; ++i) {
      runOnTargetLoop(*i);
    }

    // inline functions that capture globals in their arguments (usually memory
    // allocation or free related functions). Simplifies work of GlobalMallocAA
    for (GlobalIt global = mod.global_begin(); global != mod.global_end();
         ++global) {
      runOnGlobal(*global);
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

  /*
  TODO: call queryMemoryDep(src, dst, LoopAA::Before, LoopAA::After, loop, aa) the fun call is chosen as both the src and dst and checked against all the currently exposed insts of the loop.
  whenever a function call is involved in a dep and it is not inlined, inline it
  maintain two lists. CouldbeInlinedFunCALLs -> already processed and the toBeInlined ones.
  */

bool queryMemoryDep(Instruction *src, Instruction *dst,
                                       LoopAA::TemporalRelation FW,
                                       LoopAA::TemporalRelation RV, Loop *loop,
                                       LoopAA *aa) {
  if (!src->mayReadOrWriteMemory())
    return false;
  if (!dst->mayReadOrWriteMemory())
    return false;
  if (!src->mayWriteToMemory() && !dst->mayWriteToMemory())
    return false;

  bool loopCarried = FW != RV;

  // forward dep test
  LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop);
  if (LoopAA::NoModRef == forward)
    return false;

  // forward is Mod, ModRef, or Ref

  if ((forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
      !src->mayWriteToMemory()) {
    DEBUG(errs() << "forward modref result is mod or modref but src "
                    "instruction does not "
                    "write to memory");
    if (forward == LoopAA::ModRef)
      forward = LoopAA::Ref;
    else {
      forward = LoopAA::NoModRef;
      return false;
    }
  }

  if ((forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
      !src->mayReadFromMemory()) {
    DEBUG(errs() << "forward modref result is ref or modref but src "
                    "instruction does not "
                    "read from memory");
    if (forward == LoopAA::ModRef)
      forward = LoopAA::Mod;
    else {
      forward = LoopAA::NoModRef;
      return false;
    }
  }

  // reverse dep test
  LoopAA::ModRefResult reverse = forward;

  // in some cases calling reverse is not needed depending on whether dst writes
  // or/and reads to/from memory but in favor of correctness (AA stack does not
  // just check aliasing) instead of performance we call reverse and use
  // assertions to identify accuracy bugs of AA stack
  if (loopCarried || src != dst)
    reverse = aa->modref(dst, RV, src, loop);

  if ((reverse == LoopAA::Mod || reverse == LoopAA::ModRef) &&
      !dst->mayWriteToMemory()) {
    DEBUG(errs() << "reverse modref result is mod or modref but dst "
                    "instruction does not "
                    "write to memory");
    if (reverse == LoopAA::ModRef)
      reverse = LoopAA::Ref;
    else
      reverse = LoopAA::NoModRef;
  }

  if ((reverse == LoopAA::Ref || reverse == LoopAA::ModRef) &&
      !dst->mayReadFromMemory()) {
    DEBUG(errs() << "reverse modref result is ref or modref but src "
                    "instruction does not "
                    "read from memory");
    if (reverse == LoopAA::ModRef)
      reverse = LoopAA::Mod;
    else
      reverse = LoopAA::NoModRef;
  }

  if (LoopAA::NoModRef == reverse)
    return false;

  if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
    return false; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.

  return true;
}

};

char Inliner::ID = 0;
static RegisterPass<Inliner> mpp("aggr-inliner",
                                 "Aggressive inlining in hot loops");

} // namespace SpecPriv
} // namespace liberty
