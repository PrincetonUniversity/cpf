#define DEBUG_TYPE "loop-variant-allocation"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Utilities/CallSiteFactory.h"

#include "LoopVariantAllocation.h"

namespace liberty
{

using namespace llvm;


LoopVariantAllocation::LoopVariantAllocation() : ModulePass(ID) {
}

bool LoopVariantAllocation::runOnModule(Module &M) {
  tli = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  DL = &M.getDataLayout();
  InitializeLoopAA(this, *DL);
  return false;
}

LoopAA::ModRefResult LoopVariantAllocation::getModRefInfo(llvm::CallSite CS1,
                                   TemporalRelation Rel,
                                   llvm::CallSite CS2,
                                   const llvm::Loop *L)
{
  return ModRef;
}

LoopAA::ModRefResult LoopVariantAllocation::getModRefInfo(llvm::CallSite CS,
                                   TemporalRelation Rel,
                                   const Pointer &P,
                                   const llvm::Loop *L)
{
  return ModRef;
}

static bool isNoaliasWithinLoop(const Value *src, const Loop *L,
                                const TargetLibraryInfo &tli) {
  if( const AllocaInst *alloca = dyn_cast< AllocaInst >(src) )
    if( L->contains(alloca) )
      return true;

  CallSite cs = getCallSite(src);
  if( cs.getInstruction() )
    if (L->contains(cs.getInstruction())) {
      if (cs.getCalledFunction())
        if (cs.getCalledFunction()->getAttributes().hasAttribute(
                0, Attribute::NoAlias))
          return true;

      if (isNoAliasFn(src, &tli))
        return true;
    }

  return false;
}


LoopAA::AliasResult LoopVariantAllocation::aliasCheck(
  const Pointer &P1,
  TemporalRelation Rel,
  const Pointer &P2,
  const Loop *L)
{
  if( Rel == Same || L == 0 )
    return MayAlias;

  const Value *src1 = GetUnderlyingObject(P1.ptr, *DL, 0),
              *src2 = GetUnderlyingObject(P2.ptr, *DL, 0);


  DEBUG(errs() << "LoopVariantAllocation(" << *src1 << ", " << *src2 << ")\n");

  if (isNoaliasWithinLoop(src1, L, *tli) &&
      isNoaliasWithinLoop(src2, L, *tli)) {
    DEBUG(errs() << "Yes.\n");
    return NoAlias;
  }

  return MayAlias;

}


void LoopVariantAllocation::getAnalysisUsage(AnalysisUsage &AU) const {
  LoopAA::getAnalysisUsage(AU);
  AU.setPreservesAll();                         // Does not transform code
}

/// getAdjustedAnalysisPointer - This method is used when a pass implements an
/// analysis interface through multiple inheritance.  If needed, it should
/// override this to adjust the this pointer as needed for the specified pass
/// info.
void *LoopVariantAllocation::getAdjustedAnalysisPointer(AnalysisID PI) {
  if (PI == &LoopAA::ID)
    return (LoopAA*)this;
  return this;
}

char LoopVariantAllocation::ID = 0;

namespace {
  RegisterPass<LoopVariantAllocation>
  X("loop-variant-allocation-aa", "Alias analysis of allocation routines within loops");
  RegisterAnalysisGroup<LoopAA> Y(X);
}

}
