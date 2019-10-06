#define DEBUG_TYPE "spec-priv-local-aa"

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/LocalityRemed.h"
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

LoopAA::AliasResult ShortLivedAA::aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L,
    Remedies &R)
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
  if( read.getUnderlyingAUs(P1.ptr,ctx,aus1) )
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if( read.getUnderlyingAUs(P2.ptr,ctx,aus2) )
    t2 = asgn.classify(aus2);

  // Loop-carried queries:
  if( rel != LoopAA::Same )
  {
    // local heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if( t1 == HeapAssignment::Local )
    {
      ++numPrivatized;
      remedy->ptr = const_cast<Value *>(P1.ptr);
      R.insert(remedy);
      return NoAlias;
    }

    if( t2 == HeapAssignment::Local )
    {
      ++numPrivatized;
      remedy->ptr = const_cast<Value *>(P2.ptr);
      R.insert(remedy);
      return NoAlias;
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to different heaps?
  if ((t1 == HeapAssignment::Local || t2 == HeapAssignment::Local) &&
      t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    remedy->ptr1 = const_cast<Value *>(P1.ptr);
    remedy->ptr2 = const_cast<Value *>(P2.ptr);
    remedy->type = LocalityRemedy::Separated;
    R.insert(remedy);
    return NoAlias;
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

  return MayAlias;
}

}
