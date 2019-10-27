#define DEBUG_TYPE "spec-priv-locality-aa"

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/LocalityAA.h"
#include "liberty/Orchestration/LocalityRemed.h"

#ifndef DEFAULT_LOCALITY_REMED_COST
#define DEFAULT_LOCALITY_REMED_COST 50
#endif

#ifndef PRIVATE_ACCESS_COST
#define PRIVATE_ACCESS_COST 100
#endif

#ifndef LOCAL_ACCESS_COST
#define LOCAL_ACCESS_COST 1
#endif

#ifndef KILLPRIV_ACCESS_COST
#define KILLPRIV_ACCESS_COST 5
#endif

#ifndef SHAREPRIV_ACCESS_COST
#define SHAREPRIV_ACCESS_COST 35
#endif

namespace liberty
{
using namespace llvm;

STATISTIC(numEligible,        "Num eligible queries");
STATISTIC(numPrivatizedPriv,  "Num privatized (Private)");
STATISTIC(numPrivatizedRedux, "Num privatized (Redux)");
STATISTIC(numPrivatizedShort, "Num privatized (Short-lived)");
STATISTIC(numPrivatizedSharedPriv,  "Num privatized (Shared)");
STATISTIC(numSeparated,       "Num separated");
STATISTIC(numReusedPriv,      "Num avoid extra private inst");
STATISTIC(numUnclassifiedPtrs,"Num of unclassified pointers");
STATISTIC(numSubSep,          "Num separated via subheaps");

void LocalityAA::populateCheapPrivRemedies(Ptrs aus, Remedies &R) {
  Remedies privR = asgn.getRemedForPrivAUs(aus);
  for (auto remed : privR)
    R.insert(remed);
}

void LocalityAA::populateNoWAWRemedies(Ptrs aus, Remedies &R) {
  Remedies noWawR = asgn.getRemedForNoWAW(aus);
  for (auto remed : noWawR)
    R.insert(remed);
}

LoopAA::AliasResult LocalityAA::alias(const Value *P1, unsigned S1,
                                      TemporalRelation rel, const Value *P2,
                                      unsigned S2, const Loop *L, Remedies &R) {

  //  if( !L || !asgn.isValidFor(L) )
  //    return MayAlias;

  if( !isa<PointerType>( P1->getType() ) )
    return LoopAA::alias(P1, S1, rel, P2, S2, L, R);
  if( !isa<PointerType>( P2->getType() ) )
    return LoopAA::alias(P1, S1, rel, P2, S2, L, R);

  //const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  std::shared_ptr<LocalityRemedy> remedy =
      std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
  remedy->cost = DEFAULT_LOCALITY_REMED_COST;

  remedy->privateI = nullptr;
  remedy->privateLoad = nullptr;
  remedy->reduxS = nullptr;
  remedy->ptr1 = nullptr;
  remedy->ptr2 = nullptr;
  remedy->ptr = nullptr;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;

  if( read.getUnderlyingAUs(P1,ctx,aus1) )
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(P2,ctx,aus2) )
    t2 = asgn.classify(aus2);

  // Loop-carried queries:
  if( rel != LoopAA::Same )
  {
    // Reduction, local and private heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if (t1 == HeapAssignment::Redux || t1 == HeapAssignment::Local ||
        t1 == HeapAssignment::KillPrivate ||
        t1 == HeapAssignment::SharePrivate) {
      if (t1 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->type = LocalityRemedy::Local;
      } else if (t1 == HeapAssignment::KillPrivate) {
        ++numPrivatizedShort;
        remedy->cost += KILLPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::KillPriv;
        populateCheapPrivRemedies(aus1, R);
        populateNoWAWRemedies(aus1, R);
      } else if (t1 == HeapAssignment::SharePrivate) {
        ++numPrivatizedSharedPriv;
        remedy->cost += SHAREPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::SharePriv;
        populateCheapPrivRemedies(aus1, R);
        populateNoWAWRemedies(aus1, R);
      } else {
        ++numPrivatizedRedux;
        //if (auto sA = dyn_cast<StoreInst>(A))
        //  remedy->reduxS = const_cast<StoreInst *>(sA);
        remedy->type = LocalityRemedy::Redux;
      }
      remedy->ptr = const_cast<Value *>(P1);
      R.insert(remedy);
      return NoAlias;
    }

    if (t2 == HeapAssignment::Redux || t2 == HeapAssignment::Local ||
        t2 == HeapAssignment::KillPrivate ||
        t2 == HeapAssignment::SharePrivate) {
      if (t2 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->type = LocalityRemedy::Local;
      } else if (t2 == HeapAssignment::KillPrivate) {
        ++numPrivatizedShort;
        remedy->cost += KILLPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::KillPriv;
        populateCheapPrivRemedies(aus2, R);
        populateNoWAWRemedies(aus2, R);
      } else if (t2 == HeapAssignment::SharePrivate) {
        ++numPrivatizedSharedPriv;
        remedy->cost += SHAREPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::SharePriv;
        populateCheapPrivRemedies(aus2, R);
        populateNoWAWRemedies(aus2, R);
      } else {
        ++numPrivatizedRedux;
        //if (auto sB = dyn_cast<StoreInst>(B))
        //  remedy->reduxS = const_cast<StoreInst *>(sB);
        remedy->type = LocalityRemedy::Redux;
      }
      remedy->ptr = const_cast<Value *>(P2);
      R.insert(remedy);
      return NoAlias;
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to different heaps?
  if( t1 != t2 && t1 != HeapAssignment::Unclassified && t2 != HeapAssignment::Unclassified )
  {
    ++numSeparated;
    remedy->ptr1 = const_cast<Value *>(P1);
    remedy->ptr2 = const_cast<Value *>(P2);
    remedy->type = LocalityRemedy::Separated;
    R.insert(remedy);
    return NoAlias;
  }

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
        remedy->ptr1 = const_cast<Value *>(P1);
        remedy->ptr2 = const_cast<Value *>(P2);
        remedy->type = LocalityRemedy::Subheaps;
        R.insert(remedy);
        return NoAlias;
      }
    }
  }

  return LoopAA::alias(P1, S1, rel, P2, S2, L, R);
}

LoopAA::ModRefResult LocalityAA::modref(const Instruction *A,
                                        TemporalRelation rel, const Value *ptrB,
                                        unsigned sizeB, const Loop *L,
                                        Remedies &R) {

  const Value *ptrA = liberty::getMemOper(A);

  if (!ptrA || !ptrB)
    return LoopAA::modref(A, rel, ptrB, sizeB, L, R);

  if( !isa<PointerType>( ptrA->getType() ) )
    return LoopAA::modref(A, rel, ptrB, sizeB, L, R);
  if( !isa<PointerType>( ptrB->getType() ) )
    return LoopAA::modref(A, rel, ptrB, sizeB, L, R);

  //const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  std::shared_ptr<LocalityRemedy> remedy =
      std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
  remedy->cost = DEFAULT_LOCALITY_REMED_COST;

  remedy->privateI = nullptr;
  remedy->privateLoad = nullptr;
  remedy->reduxS = nullptr;
  remedy->ptr1 = nullptr;
  remedy->ptr2 = nullptr;
  remedy->ptr = nullptr;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;

  if( read.getUnderlyingAUs(ptrA,ctx,aus1) )
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(ptrB,ctx,aus2) )
    t2 = asgn.classify(aus2);

  // Loop-carried queries:
  if( rel != LoopAA::Same )
  {
    // Reduction, local and private heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if (t1 == HeapAssignment::Redux || t1 == HeapAssignment::Local ||
        t1 == HeapAssignment::KillPrivate ||
        t1 == HeapAssignment::SharePrivate) {
      if (t1 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->type = LocalityRemedy::Local;
        //remedy->localI = A;
      } else if (t1 == HeapAssignment::KillPrivate) {
        ++numPrivatizedShort;
        remedy->cost += KILLPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::KillPriv;
        populateCheapPrivRemedies(aus1, R);
        populateNoWAWRemedies(aus1, R);
      } else if (t1 == HeapAssignment::SharePrivate) {
        ++numPrivatizedSharedPriv;
        remedy->cost += SHAREPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::SharePriv;
        populateCheapPrivRemedies(aus1, R);
        populateNoWAWRemedies(aus1, R);
      } else {
        ++numPrivatizedRedux;
        if (auto sA = dyn_cast<StoreInst>(A))
          remedy->reduxS = const_cast<StoreInst *>(sA);
        remedy->type = LocalityRemedy::Redux;
      }
      remedy->ptr = const_cast<Value *>(ptrA);
      R.insert(remedy);
      return NoModRef;
    }

    if (t2 == HeapAssignment::Redux || t2 == HeapAssignment::Local ||
        t2 == HeapAssignment::KillPrivate ||
        t2 == HeapAssignment::SharePrivate) {
      if (t2 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->type = LocalityRemedy::Local;
      } else if (t2 == HeapAssignment::KillPrivate) {
        ++numPrivatizedShort;
        remedy->cost += KILLPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::KillPriv;
        populateCheapPrivRemedies(aus2, R);
        populateNoWAWRemedies(aus2, R);
      } else if (t2 == HeapAssignment::SharePrivate) {
        ++numPrivatizedSharedPriv;
        remedy->cost += SHAREPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::SharePriv;
        populateCheapPrivRemedies(aus2, R);
        populateNoWAWRemedies(aus2, R);
      } else {
        ++numPrivatizedRedux;
        //if (auto sB = dyn_cast<StoreInst>(B))
        //  remedy->reduxS = const_cast<StoreInst *>(sB);
        remedy->type = LocalityRemedy::Redux;
      }
      remedy->ptr = const_cast<Value *>(ptrB);
      R.insert(remedy);
      return NoModRef;
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to different heaps?
  if( t1 != t2 && t1 != HeapAssignment::Unclassified && t2 != HeapAssignment::Unclassified )
  {
    ++numSeparated;
    remedy->ptr1 = const_cast<Value *>(ptrA);
    remedy->ptr2 = const_cast<Value *>(ptrB);
    remedy->type = LocalityRemedy::Separated;
    R.insert(remedy);
    return NoModRef;
  }

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
        remedy->ptr1 = const_cast<Value *>(ptrA);
        remedy->ptr2 = const_cast<Value *>(ptrB);
        remedy->type = LocalityRemedy::Subheaps;
        R.insert(remedy);
        return NoModRef;
      }
    }
  }

  // if one of the memory accesses is private, then there is no loop-carried.
  // Validation for private accesses is more expensive than read-only and local
  // and thus private accesses are checked last
  if ( rel != LoopAA::Same ) {
    if (t1 == HeapAssignment::Private) {
      ++numPrivatizedPriv;
      remedy->cost += PRIVATE_ACCESS_COST;
      remedy->privateI = const_cast<Instruction *>(A);
      remedy->type = LocalityRemedy::Private;
      privateInsts.insert(A);
      if (isa<LoadInst>(A))
        remedy->privateLoad = dyn_cast<LoadInst>(A);
      R.insert(remedy);
      return NoModRef;
    }
  }

  return LoopAA::modref(A, rel, ptrB, sizeB, L, R);
}

LoopAA::ModRefResult
LocalityAA::modref_with_ptrs(const Instruction *A, const Value *ptrA,
                             TemporalRelation rel, const Instruction *B,
                             const Value *ptrB, const Loop *L, Remedies &R) {

  if (!ptrA || !ptrB)
    return LoopAA::modref(A, rel, B, L, R);

  if (!isa<PointerType>(ptrA->getType()))
    return LoopAA::modref(A, rel, B, L, R);
  if (!isa<PointerType>(ptrB->getType()))
    return LoopAA::modref(A, rel, B, L, R);

  // const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  std::shared_ptr<LocalityRemedy> remedy =
      std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
  remedy->cost = DEFAULT_LOCALITY_REMED_COST;

  remedy->privateI = nullptr;
  remedy->privateLoad = nullptr;
  remedy->reduxS = nullptr;
  remedy->ptr1 = nullptr;
  remedy->ptr2 = nullptr;
  remedy->ptr = nullptr;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;

  if (read.getUnderlyingAUs(ptrA, ctx, aus1))
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptrB, ctx, aus2))
    t2 = asgn.classify(aus2);

  // Loop-carried queries:
  if (rel != LoopAA::Same) {
    // Reduction, local and private heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if (t1 == HeapAssignment::Redux || t1 == HeapAssignment::Local ||
        t1 == HeapAssignment::KillPrivate ||
        t1 == HeapAssignment::SharePrivate) {
      if (t1 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->type = LocalityRemedy::Local;
        // remedy->localI = A;
      } else if (t1 == HeapAssignment::KillPrivate) {
        ++numPrivatizedShort;
        remedy->cost += KILLPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::KillPriv;
        populateCheapPrivRemedies(aus1, R);
        populateNoWAWRemedies(aus1, R);
      } else if (t1 == HeapAssignment::SharePrivate) {
        ++numPrivatizedSharedPriv;
        remedy->cost += SHAREPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::SharePriv;
        populateCheapPrivRemedies(aus1, R);
        populateNoWAWRemedies(aus1, R);
      } else {
        ++numPrivatizedRedux;
        if (auto sA = dyn_cast<StoreInst>(A))
          remedy->reduxS = const_cast<StoreInst *>(sA);
        remedy->type = LocalityRemedy::Redux;
      }
      remedy->ptr = const_cast<Value *>(ptrA);
      R.insert(remedy);
      return NoModRef;
    }

    if (t2 == HeapAssignment::Redux || t2 == HeapAssignment::Local ||
        t2 == HeapAssignment::KillPrivate ||
        t2 == HeapAssignment::SharePrivate) {
      if (t2 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->type = LocalityRemedy::Local;
      } else if (t2 == HeapAssignment::KillPrivate) {
        ++numPrivatizedShort;
        remedy->cost += KILLPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::KillPriv;
        populateCheapPrivRemedies(aus2, R);
        populateNoWAWRemedies(aus2, R);
      } else if (t2 == HeapAssignment::SharePrivate) {
        ++numPrivatizedSharedPriv;
        remedy->cost += SHAREPRIV_ACCESS_COST;
        remedy->type = LocalityRemedy::SharePriv;
        populateCheapPrivRemedies(aus2, R);
        populateNoWAWRemedies(aus2, R);
      } else {
        ++numPrivatizedRedux;
        if (auto sB = dyn_cast<StoreInst>(B))
          remedy->reduxS = const_cast<StoreInst *>(sB);
        remedy->type = LocalityRemedy::Redux;
      }
      remedy->ptr = const_cast<Value *>(ptrB);
      R.insert(remedy);
      return NoModRef;
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to
  // different heaps?
  if (t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    remedy->ptr1 = const_cast<Value *>(ptrA);
    remedy->ptr2 = const_cast<Value *>(ptrB);
    remedy->type = LocalityRemedy::Separated;
    R.insert(remedy);
    return NoModRef;
  }

  // They are assigned to the same heap.
  // Are they assigned to different sub-heaps?
  if (t1 == t2 && t1 != HeapAssignment::Unclassified) {
    // sdflsdakjfjsdlkjfl
    const int subheap1 = asgn.getSubHeap(aus1);
    if (subheap1 > 0) {
      const int subheap2 = asgn.getSubHeap(aus2);
      if (subheap2 > 0 && subheap1 != subheap2) {
        ++numSubSep;
        remedy->ptr1 = const_cast<Value *>(ptrA);
        remedy->ptr2 = const_cast<Value *>(ptrB);
        remedy->type = LocalityRemedy::Subheaps;
        R.insert(remedy);
        return NoModRef;
      }
    }
  }

  // if one of the memory accesses is private, then there is no loop-carried.
  // Validation for private accesses is more expensive than read-only and local
  // and thus private accesses are checked last
  if (rel != LoopAA::Same) {
    // if memory access in instruction B was already identified as private,
    // re-use it instead of introducing another private inst.
    if (t1 == HeapAssignment::Private && !privateInsts.count(B)) {
      ++numPrivatizedPriv;
      remedy->cost += PRIVATE_ACCESS_COST;
      remedy->privateI = const_cast<Instruction *>(A);
      remedy->type = LocalityRemedy::Private;
      privateInsts.insert(A);
      if (isa<LoadInst>(A))
        remedy->privateLoad = dyn_cast<LoadInst>(A);
      else if (t2 == HeapAssignment::Private && isa<LoadInst>(B))
        remedy->privateLoad = dyn_cast<LoadInst>(B);
      R.insert(remedy);
      return NoModRef;
    } else if (t2 == HeapAssignment::Private) {
      if (t1 == HeapAssignment::Private && privateInsts.count(B)) {
        ++numReusedPriv;
      }
      ++numPrivatizedPriv;
      remedy->cost += PRIVATE_ACCESS_COST;
      remedy->privateI = const_cast<Instruction *>(B);
      remedy->type = LocalityRemedy::Private;
      privateInsts.insert(B);
      if (isa<LoadInst>(B))
        remedy->privateLoad = dyn_cast<LoadInst>(B);
      else if (t1 == HeapAssignment::Private && isa<LoadInst>(A))
        remedy->privateLoad = dyn_cast<LoadInst>(A);
      R.insert(remedy);
      return NoModRef;
    }
  }

  return LoopAA::modref(A, rel, B, L, R);
}

LoopAA::ModRefResult LocalityAA::modref(const Instruction *A,
                                        TemporalRelation rel,
                                        const Instruction *B, const Loop *L,
                                        Remedies &R) {

  return modref_many(A, rel, B, L, R);
}

} // namespace liberty
