#define DEBUG_TYPE "svf-results"

#include "llvm/ADT/Statistic.h"

#include "liberty/Analysis/SVFResults.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GetMemOper.h"

namespace liberty {
using namespace llvm;

STATISTIC(numNoModRef, "Number of NoModRef from svf-results");
STATISTIC(numNoAlias, "Number of no alias from svf-results");
STATISTIC(numMustAlias, "Number of must alias from svf-results");

SVFResults::SVFResults() : ModulePass(ID) {}
SVFResults::~SVFResults() {}

LoopAA::AliasResult SVFResults::alias(const Value *ptrA, unsigned sizeA,
                                      TemporalRelation rel, const Value *ptrB,
                                      unsigned sizeB, const Loop *L,
                                      Remedies &R,
                                      DesiredAliasResult dAliasRes) {

  auto wpaRes = wpa->alias(MemoryLocation(ptrA, LocationSize(sizeA)),
                           MemoryLocation(ptrB, LocationSize(sizeB)));

  if (wpaRes == llvm::NoAlias) {
    ++numNoAlias;
    return LoopAA::NoAlias;
  } else if (wpaRes == llvm::MustAlias) {
    // if rel is not the same then it either means MustAlias (constant pointers)
    // or NoAlias (if different value in every iteration)
    if (rel == LoopAA::Same) {
      ++numMustAlias;
      return LoopAA::MustAlias;
    }
  }

  return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);
}

LoopAA::ModRefResult SVFResults::modref(const Instruction *A,
                                        TemporalRelation rel, const Value *ptrB,
                                        unsigned sizeB, const Loop *L,
                                        Remedies &R) {

  auto wpaRes = wpa->alias(MemoryLocation::get(A),
                           MemoryLocation(ptrB, LocationSize(sizeB)));

  if (wpaRes == llvm::NoAlias) {
    ++numNoModRef;
    return LoopAA::NoModRef;
  }

  return LoopAA::modref(A, rel, ptrB, sizeB, L, R);
}

LoopAA::ModRefResult SVFResults::modref(const Instruction *A,
                                        TemporalRelation rel,
                                        const Instruction *B, const Loop *L,
                                        Remedies &R) {

  auto wpaRes = llvm::MayAlias;

  // skip if call since MemoryLocation::get() fails on CallInst's
  if ( !isa<CallInst>(A) && !isa<CallInst>(B) )
    wpaRes = wpa->alias(MemoryLocation::get(A), MemoryLocation::get(B));

  if (wpaRes == llvm::NoAlias) {
    ++numNoModRef;
    return LoopAA::NoModRef;
  }

  return LoopAA::modref(A, rel, B, L, R);
}

static RegisterPass<SVFResults>
    X("svf-results", "SVF's Results formulated as a CAF analysis pass");
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char SVFResults::ID = 0;

} // namespace liberty
