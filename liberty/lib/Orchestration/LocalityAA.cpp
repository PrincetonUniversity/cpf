#define DEBUG_TYPE "spec-priv-locality-aa"

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/LocalityAA.h"

namespace liberty
{
using namespace llvm;

STATISTIC(numQueries,    "Num queries");
STATISTIC(numEligible,   "Num eligible queries");
STATISTIC(numReduxLocalPrivatized, "Num privatized for redux and short-lived family");
STATISTIC(numSeparated,  "Num separated");
STATISTIC(numSubSep,     "Num separated via subheaps");

LoopAA::AliasResult LocalityAA::aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L)
{
  ++numQueries;

  if( !L || !asgn.isValidFor(L) )
    return MayAlias;

  if( !isa<PointerType>( P1.ptr->getType() ) )
    return MayAlias;
  if( !isa<PointerType>( P2.ptr->getType() ) )
    return MayAlias;

  const Ctx *ctx = read.getCtx(L);

  ++numEligible;

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
    // Reduction, local and private heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    //
    // dont consider privatization for now since it is hard implementation-wise
    // to collect the set of required private reads and writes
    if( t1 == HeapAssignment::Redux || t1 == HeapAssignment::Local || t1 == HeapAssignment::Private )
    //if( t1 == HeapAssignment::Redux || t1 == HeapAssignment::Local)
    {
      ++numReduxLocalPrivatized;
      return NoAlias;
    }

    if( t2 == HeapAssignment::Redux || t2 == HeapAssignment::Local || t2 == HeapAssignment::Private )
    //if( t2 == HeapAssignment::Redux || t2 == HeapAssignment::Local)
    {
      ++numReduxLocalPrivatized;
      return NoAlias;
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to different heaps?
  if( t1 != t2 && t1 != HeapAssignment::Unclassified && t2 != HeapAssignment::Unclassified )
  {
    ++numSeparated;
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
        return NoAlias;
      }
    }
  }

  return MayAlias;
}

}
