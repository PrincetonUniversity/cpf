#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopInfoImpl.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "scaf/Utilities/IV.h"

using namespace llvm;

/// Get the latch condition instruction.
ICmpInst *liberty::getLatchCmpInst(const Loop &L) {
  if (BasicBlock *Latch = L.getLoopLatch()) {
    if (BranchInst *BI = dyn_cast_or_null<BranchInst>(Latch->getTerminator())) {
      if (BI->isConditional()) {
        return dyn_cast<ICmpInst>(BI->getCondition());
      } else {
        // not rotated loop (get the exit condition of the header instead)
        auto *header = L.getHeader();
        if (L.isLoopExiting(header))
          if (BranchInst *BIH =
                  dyn_cast_or_null<BranchInst>(header->getTerminator()))
            if (BIH->isConditional())
              return dyn_cast<ICmpInst>(BIH->getCondition());
      }
    }
  }
  return nullptr;
}

PHINode *liberty::getInductionVariable(const Loop *L,
                                       ScalarEvolution &SE) {
  if (!L->isLoopSimplifyForm())
    return nullptr;

  BasicBlock *Header = L->getHeader();
  assert(Header && "Expected a valid loop header");
  ICmpInst *CmpInst = liberty::getLatchCmpInst(*L);
  if (!CmpInst)
    return nullptr;

  Instruction *LatchCmpOp0 = dyn_cast<Instruction>(CmpInst->getOperand(0));
  Instruction *LatchCmpOp1 = dyn_cast<Instruction>(CmpInst->getOperand(1));

  for (PHINode &IndVar : Header->phis()) {
    InductionDescriptor IndDesc;
    if (!InductionDescriptor::isInductionPHI(&IndVar, L, &SE, IndDesc))
      continue;

    Instruction *StepInst = IndDesc.getInductionBinOp();

    // case 1:
    // IndVar = phi[{InitialValue, preheader}, {StepInst, latch}]
    // StepInst = IndVar + step
    // cmp = StepInst < FinalValue
    if (StepInst == LatchCmpOp0 || StepInst == LatchCmpOp1)
      return &IndVar;

    // case 2:
    // IndVar = phi[{InitialValue, preheader}, {StepInst, latch}]
    // StepInst = IndVar + step
    // cmp = IndVar < FinalValue
    if (&IndVar == LatchCmpOp0 || &IndVar == LatchCmpOp1)
      return &IndVar;
  }

  return nullptr;
}

Optional<Loop::LoopBounds> liberty::getBounds(const Loop *L,
                                              ScalarEvolution &SE) {
  if (PHINode *IndVar = liberty::getInductionVariable(L, SE))
    return Loop::LoopBounds::getBounds(*L, *IndVar, SE);

  return None;
}

bool liberty::getInductionDescriptor(const Loop *L, ScalarEvolution &SE,
                                     InductionDescriptor &IndDesc) {
  if (PHINode *IndVar = liberty::getInductionVariable(L, SE))
    return InductionDescriptor::isInductionPHI(IndVar, L, &SE, IndDesc);

  return false;
}
