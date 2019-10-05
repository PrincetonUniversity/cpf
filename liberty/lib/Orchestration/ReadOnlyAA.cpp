#define DEBUG_TYPE "spec-priv-read-only-aa"

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/ReadOnlyAA.h"
#include "liberty/Utilities/GetMemOper.h"

namespace liberty
{
using namespace llvm;

STATISTIC(numQueries,    "Num queries");
STATISTIC(numEligible,   "Num eligible queries");
STATISTIC(numPrivatized, "Num privatized");
STATISTIC(numSeparated,  "Num separated");
STATISTIC(numNoRead,     "Num no-read");

LoopAA::AliasResult ReadOnlyAA::alias(const Value *ptrA, unsigned sizeA,
                                      TemporalRelation rel, const Value *ptrB,
                                      unsigned sizeB, const Loop *L,
                                      Remedies &R) {

  ++numQueries;

//  if( !L || !asgn.isValidFor(L) )
//    return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L);

  if( !isa<PointerType>( ptrA->getType() ) )
    return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L,R);
  if( !isa<PointerType>( ptrB->getType() ) )
    return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L,R);

  //const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(ptrA,ctx,aus1) )
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(ptrB,ctx,aus2) )
    t2 = asgn.classify(aus2);

  if ((t1 == HeapAssignment::ReadOnly || t2 == HeapAssignment::ReadOnly) &&
      t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    return NoAlias;
  }

  return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L, R);

}

LoopAA::ModRefResult
ReadOnlyAA::check_modref(const Value *ptrA, const Value *ptrB, const Loop *L) {

  if (!ptrA || !ptrB)
    return ModRef;

  //  if( !L || !asgn.isValidFor(L) )
//    return MayAlias;

  if( !isa<PointerType>( ptrA->getType() ) )
    return ModRef;
  if( !isa<PointerType>( ptrB->getType() ) )
    return ModRef;

  //const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(ptrA,ctx,aus1) )
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(ptrB,ctx,aus2) )
    t2 = asgn.classify(aus2);

  if ((t1 == HeapAssignment::ReadOnly || t2 == HeapAssignment::ReadOnly) &&
      t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    return NoModRef;
  }

  if (t1 == HeapAssignment::ReadOnly || t2 == HeapAssignment::ReadOnly) {
    ++numNoRead;
    //return ModRefResult(~Mod);
    return Ref;
  }

  return ModRef;
}

LoopAA::ModRefResult ReadOnlyAA::modref(const Instruction *A,
                                        TemporalRelation rel, const Value *ptrB,
                                        unsigned sizeB, const Loop *L,
                                        Remedies &R) {

  ++numQueries;

  const Value *ptrA = liberty::getMemOper(A);

  ModRefResult result = check_modref(ptrA, ptrB, L);

  if( result != NoModRef )
    // Chain.
    result = ModRefResult(result & LoopAA::modref(A,rel,ptrB,sizeB,L,R) );

  return result;
}

LoopAA::ModRefResult ReadOnlyAA::modref(const Instruction *A,
                                        TemporalRelation rel,
                                        const Instruction *B, const Loop *L,
                                        Remedies &R) {

  ++numQueries;

  const Value *ptrA = liberty::getMemOper(A);
  const Value *ptrB = liberty::getMemOper(B);

  ModRefResult result = check_modref(ptrA, ptrB, L);

  if( result != NoModRef )
    // Chain.
    result = ModRefResult(result & LoopAA::modref(A,rel,B,L,R) );

  return result;
}

/*
LoopAA::AliasResult ReadOnlyAA::aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L)
{
  ++numQueries;

//  if( !L || !asgn.isValidFor(L) )
//    return MayAlias;

  if( !isa<PointerType>( P1.ptr->getType() ) )
    return MayAlias;
  if( !isa<PointerType>( P2.ptr->getType() ) )
    return MayAlias;

  //const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(P1.ptr,ctx,aus1) )
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(P2.ptr,ctx,aus2) )
    t2 = asgn.classify(aus2);

  if ((t1 == HeapAssignment::ReadOnly || t2 == HeapAssignment::ReadOnly) &&
      t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    return NoAlias;
  }

  return MayAlias;
}
*/

}
