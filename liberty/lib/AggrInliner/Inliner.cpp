// Aggressive Inliner
//
// Inlines functions in call sites of target hot loops

#define DEBUG_TYPE "inliner"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/SmtxAA.h"
#include "liberty/Utilities/CallSiteFactory.h"
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
  	au.addRequired< TargetLibraryInfoWrapperPass >();
    au.addRequired< ModuleLoops >();
    au.addRequired< BlockFrequencyInfoWrapperPass >();
    au.addRequired< Targets >();
  	//au.addRequired< PDGBuilder >();
  	au.addRequired< LoopAA >();
  	au.addRequired<SmtxSpeculationManager>();
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

  // check if call sources or sinks any LC flow dependences
  bool noFlowDep(CallInst *call, LoopAA *aa, Loop *loop) {

    if (queryMemoryFlowDep(call, call, LoopAA::Before, LoopAA::After, loop, aa))
      return false;

    for (BasicBlock *BB : loop->getBlocks()) {
      for (Instruction &I : *BB) {

        if (!I.mayReadOrWriteMemory())
          continue;

				if (&I == call)
					continue;

        if (queryMemoryFlowDep(call, &I, LoopAA::Before, LoopAA::After, loop,
                               aa))
          return false;
        if (queryMemoryFlowDep(&I, call, LoopAA::Before, LoopAA::After, loop,
                               aa))
          return false;
      }
    }

    return true;
  }

  void processBB(BasicBlock &BB, std::unordered_set<Function *> &curPathVisited,
                 LoopAA *aa, Loop *loop) {
    if (isSpeculativelyDeadBB(BB))
      return;

    for (Instruction &I : BB) {
      if (CallInst *call = dyn_cast<CallInst>(&I)) {
        Function *calledFun = call->getCalledFunction();
        if (calledFun && !calledFun->isDeclaration()) {
          if (noFlowDep(call, aa, loop))
						continue;
          if (validCallInsts.count(call))
            continue;
          if (processFunction(calledFun, curPathVisited, aa, loop)) {
            inlineCallInsts.push(call);
            validCallInsts.insert(call);
          }
        }
      }
    }
  }

  bool processFunction(Function *F,
                       std::unordered_set<Function *> &curPathVisited,
                       LoopAA *aa, Loop *loop) {
    // check if there is a cycle in call graph
    // or if the function is recursive (quick check)
    if (curPathVisited.count(F) || isRecursiveFnFast(F))
      return false;
    curPathVisited.insert(F);

    if (processedFunctions.count(F)) {
      curPathVisited.erase(F);
      return true;
    }
    processedFunctions.insert(F);

    for (BasicBlock &BB : *F)
      processBB(BB, curPathVisited, aa, loop);

    curPathVisited.erase(F);
    return true;
  }

  void runOnTargetLoop(Loop *loop) {
    std::unordered_set<Function *> curPathVisited;
    Function *loopFun = loop->getHeader()->getParent();
    curPathVisited.insert(loopFun);

    const DataLayout &DL = loopFun->getParent()->getDataLayout();
    SmtxSpeculationManager &smtxMan = getAnalysis<SmtxSpeculationManager>();
    SmtxAA smtxaa(&smtxMan);
    smtxaa.InitializeLoopAA(this, DL);

    LoopAA *aa = getAnalysis<LoopAA>().getTopAA();

    for (BasicBlock *BB : loop->getBlocks())
      processBB(*BB, curPathVisited, aa, loop);
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


  static bool queryMemoryFlowDep(Instruction *src, Instruction *dst,
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

    Remedies Rf;
    Remedies Rr;

    // forward dep test
    LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop, Rf);
    if (LoopAA::NoModRef == forward)
      return false;

    // forward is Mod, ModRef, or Ref

    if (!src->mayWriteToMemory())
      forward = LoopAA::ModRefResult(forward & (~LoopAA::Mod));
    if (!src->mayReadFromMemory())
      forward = LoopAA::ModRefResult(forward & (~LoopAA::Ref));

    // reverse dep test
    LoopAA::ModRefResult reverse = forward;

    // in some cases calling reverse is not needed depending on whether dst
    // writes or/and reads to/from memory but in favor of correctness (AA stack
    // does not just check aliasing) instead of performance we call reverse
    if (loopCarried || src != dst)
      reverse = aa->modref(dst, RV, src, loop, Rr);

    if (!dst->mayWriteToMemory())
      reverse = LoopAA::ModRefResult(reverse & (~LoopAA::Mod));
    if (!dst->mayReadFromMemory())
      reverse = LoopAA::ModRefResult(reverse & (~LoopAA::Ref));

    if (LoopAA::NoModRef == reverse)
      return false;

    if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
      return false; // RaR dep; who cares.

    // At this point, we know there is one or more of
    // a flow-, anti-, or output-dependence.

    // only interested in flow deps
    bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);

    if (!RAW)
      return false;

    return true;
  }
};

char Inliner::ID = 0;
static RegisterPass<Inliner> mpp("aggr-inliner",
                                 "Aggressive inlining in hot loops");

} // namespace SpecPriv
} // namespace liberty
