#define DEBUG_TYPE "llvm-aa-results"

#include "llvm/ADT/Statistic.h"

#include "liberty/Analysis/LLVMAAResults.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/CallSiteFactory.h"

#include <vector>

namespace liberty
{
using namespace llvm;

STATISTIC(numNoModRef, "Number of NoModRef from llvm-aa-results");
STATISTIC(numNoAlias, "Number of no alias from llvm-aa-results");

void LLVMAAResults::computeAAResults(const Function *cf) {
  Function *f = const_cast<Function *>(cf);
  if (f != curF) {
    aa = &(*AARGetter)(*f);
    curF = f;
  }
}

LLVMAAResults::LLVMAAResults() : ModulePass(ID) {}
LLVMAAResults::~LLVMAAResults() {}

static const Function *getParent(const Value *V) {
  if (const Instruction *inst = dyn_cast<Instruction>(V)) {
    if (!inst->getParent())
      return nullptr;
    return inst->getParent()->getParent();
  }

  if (const Argument *arg = dyn_cast<Argument>(V))
    return arg->getParent();

  return nullptr;
}

static bool notDifferentParent(const Value *O1, const Value *O2) {

  const Function *F1 = getParent(O1);
  const Function *F2 = getParent(O2);

  return !F1 || !F2 || F1 == F2;
}

LoopAA::AliasResult LLVMAAResults::alias(
    const Value *ptrA, unsigned sizeA,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L) {

  // ZY: LLVM AA seems only applicable for II deps
  // sot: mustAlias from standard LLVM AA could be misleading for loop carried deps
  if (rel != LoopAA::Same)
    return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L);


  // only handles intra-iteration mem queries
  auto *funA = getParent(ptrA);
  if (!funA || !notDifferentParent(ptrA, ptrB))
    return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L);

  if (funA != curF)
    computeAAResults(funA);

  auto aaRes = aa->alias(ptrA, sizeA, ptrB, sizeB);

  if (aaRes == llvm::NoAlias) {
    ++numNoAlias;
    return LoopAA::NoAlias;
  }

  LoopAA::AliasResult aaLoopAARes;
  if (aaRes == llvm::PartialAlias || aaRes == llvm::MayAlias)
    aaLoopAARes = LoopAA::MayAlias;
  else  //aaRes == llvm::MustAlias
    aaLoopAARes = LoopAA::MustAlias;

  return LoopAA::AliasResult(aaLoopAARes &
                             LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L));
}

LoopAA::ModRefResult LLVMAAResults::modref(const Instruction *A,
                                           TemporalRelation rel,
                                           const Value *ptrB, unsigned sizeB,
                                           const Loop *L) {
  // ZY: LLVM AA seems only applicable for II deps
  if (rel != LoopAA::Same)
    return LoopAA::modref(A, rel, ptrB, sizeB, L);

  auto *funA = A->getParent()->getParent();
  auto *funB = getParent(ptrB);

  if (!funA || funA != funB)
    return LoopAA::modref(A, rel, ptrB, sizeB, L);

  if (funA != curF)
    computeAAResults(funA);

  auto aaRes = aa->getModRefInfo(A, ptrB, sizeB);

  if (aaRes == llvm::MRI_NoModRef) {
    // DEBUG(errs()<<"NoModRef 1 by LLVM AA" << *A << " --> " << *B << "\n");
    ++numNoModRef;
    return LoopAA::NoModRef;
  }

  return LoopAA::ModRefResult(aaRes & LoopAA::modref(A, rel, ptrB, sizeB, L));
}

LoopAA::ModRefResult LLVMAAResults::modref(const Instruction *A,
                                           TemporalRelation rel,
                                           const Instruction *B,
                                           const Loop *L) {
  // ZY: LLVM AA seems only applicable for II deps
  if (rel != LoopAA::Same)
    return LoopAA::modref(A, rel, B, L);

  auto *funA = A->getParent()->getParent();
  auto *funB = B->getParent()->getParent();

  if (!funA || funA != funB)
    return LoopAA::modref(A, rel, B, L);

  if (funA != curF)
    computeAAResults(funA);

  llvm::ModRefInfo aaRes;

  Instruction *nA = const_cast<Instruction *>(A);
  Instruction *nB = const_cast<Instruction *>(B);
  auto *callA = dyn_cast<CallInst>(nA);
  auto *callB = dyn_cast<CallInst>(nB);

  if (callA && callB)
    aaRes =
        aa->getModRefInfo(ImmutableCallSite(callA), ImmutableCallSite(callB));
  else if (callB)
    aaRes = aa->getModRefInfo(nA, ImmutableCallSite(callB));
  else{
    aaRes = aa->getModRefInfo(A, MemoryLocation::get(B));
    //switch (aa->alias(MemoryLocation::get(A), MemoryLocation::get(B))) {
    //  case PartialAlias:
    //  case MayAlias:
    //  case MustAlias:
    //    break;
    //  case NoAlias:
    //    DEBUG(errs()<<"NoModRef 2 by LLVM AA" << *A << " --> " << *B << "\n");
    //    ++numNoModRef;
    //    return LoopAA::NoModRef;
    //}
  }

  if (aaRes == llvm::MRI_NoModRef) {
    // DEBUG(errs()<<"NoModRef 2 by LLVM AA" << *A << " --> " << *B << "\n");
    ++numNoModRef;
    return LoopAA::NoModRef;
  }

  //return LoopAA::ModRefResult(LoopAA::modref(A, rel, B, L));
  return LoopAA::ModRefResult(aaRes & LoopAA::modref(A, rel, B, L));
}

static RegisterPass<LLVMAAResults>
    X("llvm-aa-results", "LLVM's AA Results formulated as a CAF analysis pass");
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char LLVMAAResults::ID = 0;

} // namespace liberty
