#define DEBUG_TYPE "spec-priv-local-aa"

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/LocalityAA.h"
#include "liberty/Orchestration/ShortLivedAA.h"

#ifndef DEFAULT_LOCALITY_REMED_COST
#define DEFAULT_LOCALITY_REMED_COST 50
#endif

#ifndef LOCAL_ACCESS_COST
#define LOCAL_ACCESS_COST 1
#endif

namespace liberty
{
using namespace llvm;

STATISTIC(numQueries,    "Num queries");
STATISTIC(numEligible,   "Num eligible queries");
STATISTIC(numPrivatized, "Num privatized");
STATISTIC(numSeparated,  "Num separated");

LoopAA::AliasResult ShortLivedAA::alias(const Value *ptrA, unsigned sizeA,
                                      TemporalRelation rel, const Value *ptrB,
                                      unsigned sizeB, const Loop *L,
                                      Remedies &R,
                                      DesiredAliasResult dAliasRes) {

  ++numQueries;

  if (dAliasRes == DMustAlias)
    return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);

  //  if( !L || !asgn.isValidFor(L) )
  //    return MayAlias;

  if( !isa<PointerType>( ptrA->getType() ) )
    return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L,R);
  if( !isa<PointerType>( ptrB->getType() ) )
    return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L,R);

  //const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  std::shared_ptr<LocalityRemedy> remedy =
      std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
  //remedy->cost = DEFAULT_LOCALITY_REMED_COST + LOCAL_ACCESS_COST;
  remedy->type = LocalityRemedy::UOCheck;
  remedy->privateI = nullptr;
  remedy->privateLoad = nullptr;
  remedy->reduxS = nullptr;
  remedy->ptr1 = nullptr;
  remedy->ptr2 = nullptr;
  remedy->ptr = nullptr;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(ptrA,ctx,aus1) ) {
    if (asgn)
      t1 = asgn->classify(aus1);
    else if (localAUs && HeapAssignment::subOfAUSet(aus1, *localAUs))
      t1 = HeapAssignment::Local;
  }

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(ptrB,ctx,aus2) ) {
    if (asgn)
      t2 = asgn->classify(aus2);
    else if (localAUs && HeapAssignment::subOfAUSet(aus2, *localAUs))
      t2 = HeapAssignment::Local;
  }

  Remedies tmpR;

  // Loop-carried queries:
  if( rel != LoopAA::Same )
  {
    // local heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if( t1 == HeapAssignment::Local )
    {
      ++numPrivatized;
      remedy->ptr = const_cast<Value *>(ptrA);
      remedy->setCost(perf);
      tmpR.insert(remedy);
      return LoopAA::chain(R, ptrA, sizeA, rel, ptrB, sizeB, L, NoAlias, tmpR);
    }

    if( t2 == HeapAssignment::Local )
    {
      ++numPrivatized;
      remedy->ptr = const_cast<Value *>(ptrB);
      remedy->setCost(perf);
      tmpR.insert(remedy);
      return LoopAA::chain(R, ptrA, sizeA, rel, ptrB, sizeB, L, NoAlias, tmpR);
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to different heaps?
  if ((t1 == HeapAssignment::Local || t2 == HeapAssignment::Local) &&
      t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    std::shared_ptr<LocalityRemedy> remedy2 =
        std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
    remedy2->type = LocalityRemedy::UOCheck;
    remedy2->privateI = nullptr;
    remedy2->privateLoad = nullptr;
    remedy2->reduxS = nullptr;

    remedy->ptr = const_cast<Value *>(ptrA);
    remedy2->ptr = const_cast<Value *>(ptrB);
    remedy->setCost(perf);
    remedy2->setCost(perf);
    tmpR.insert(remedy);
    tmpR.insert(remedy2);
    return LoopAA::chain(R, ptrA, sizeA, rel, ptrB, sizeB, L, NoAlias, tmpR);
  }

  /*
  // They are assigned to the same heap.
  // Are they assigned to different sub-heaps?
  if( t1 == t2 && t1 != HeapAssignment::Unclassified )
  {
    //sdflsdakjfjsdlkjfl
    const int subheap1 = asgn.getSubHeap(aus1);
    if( subheap1 > 0 )
    {
      const int subheap2 = asgn.getSubHeap(aus2);
      if( subheap2 > 0 && subheap1 != subheap2 )
      {
        ++numSubSep;
        return NoAlias;
      }
    }
  }
  */

  return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L, R);
}

LoopAA::ModRefResult ShortLivedAA::check_modref(const Value *ptrA,
                                                TemporalRelation rel,
                                                const Value *ptrB,
                                                const Loop *L, Remedies &R) {

  if (!ptrA || !ptrB)
    return ModRef;

  //  if( !L || !asgn->isValidFor(L) )
//    return MayAlias;

  if( !isa<PointerType>( ptrA->getType() ) )
    return ModRef;
  if( !isa<PointerType>( ptrB->getType() ) )
    return ModRef;

  //const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  std::shared_ptr<LocalityRemedy> remedy =
      std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
  remedy->cost = DEFAULT_LOCALITY_REMED_COST + LOCAL_ACCESS_COST;
  remedy->type = LocalityRemedy::Local;

  remedy->privateI = nullptr;
  remedy->privateLoad = nullptr;
  remedy->reduxS = nullptr;
  remedy->ptr1 = nullptr;
  remedy->ptr2 = nullptr;
  remedy->ptr = nullptr;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptrA, ctx, aus1)) {
    if (asgn)
      t1 = asgn->classify(aus1);
    else if (localAUs && HeapAssignment::subOfAUSet(aus1, *localAUs))
      t1 = HeapAssignment::Local;
  }

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptrB, ctx, aus2)) {
    if (asgn)
      t2 = asgn->classify(aus2);
    else if (localAUs && HeapAssignment::subOfAUSet(aus2, *localAUs))
      t2 = HeapAssignment::Local;
  }

  // Loop-carried queries:
  if (rel != LoopAA::Same) {
    // local heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if (t1 == HeapAssignment::Local) {
      ++numPrivatized;
      remedy->ptr = const_cast<Value *>(ptrA);
      remedy->setCost(perf);
      R.insert(remedy);
      return NoModRef;
    }

    if (t2 == HeapAssignment::Local) {
      ++numPrivatized;
      remedy->ptr = const_cast<Value *>(ptrB);
      remedy->setCost(perf);
      R.insert(remedy);
      return NoModRef;
    }
  }

  if ((t1 == HeapAssignment::Local || t2 == HeapAssignment::Local) &&
      t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    std::shared_ptr<LocalityRemedy> remedy2 =
        std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
    remedy2->type = LocalityRemedy::UOCheck;
    remedy2->privateI = nullptr;
    remedy2->privateLoad = nullptr;
    remedy2->reduxS = nullptr;

    remedy->ptr = const_cast<Value *>(ptrA);
    remedy2->ptr = const_cast<Value *>(ptrB);
    remedy->setCost(perf);
    remedy2->setCost(perf);
    R.insert(remedy);
    R.insert(remedy2);
    return NoModRef;
  }

  return ModRef;
}

LoopAA::ModRefResult ShortLivedAA::modref(const Instruction *A,
                                          TemporalRelation rel,
                                          const Value *ptrB, unsigned sizeB,
                                          const Loop *L, Remedies &R) {

  ++numQueries;

  const Value *ptrA = liberty::getMemOper(A);

  Remedies tmpR;
  ModRefResult result = check_modref(ptrA, rel, ptrB, L, tmpR);
  return LoopAA::chain(R, A, rel, ptrB, sizeB, L, result, tmpR);
}

LoopAA::ModRefResult
ShortLivedAA::modref_with_ptrs(const Instruction *A, const Value *ptrA,
                               TemporalRelation rel, const Instruction *B,
                               const Value *ptrB, const Loop *L, Remedies &R) {

  Remedies tmpR;
  ModRefResult result = check_modref(ptrA, rel, ptrB, L, tmpR);
  return LoopAA::chain(R, A, rel, B, L, result, tmpR);
}

LoopAA::ModRefResult ShortLivedAA::modref(const Instruction *A,
                                          TemporalRelation rel,
                                          const Instruction *B, const Loop *L,
                                          Remedies &R) {
  ++numQueries;
  return modref_many(A, rel, B, L, R);
}
}
