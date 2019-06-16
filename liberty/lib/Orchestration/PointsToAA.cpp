#define DEBUG_TYPE "spec-priv-points-to-aa"

#include "llvm/ADT/Statistic.h"

#include "liberty/Utilities/FindUnderlyingObjects.h"
#include "liberty/Orchestration/PointsToAA.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numNoAlias, "Num no-alias / no-modref");


LoopAA::AliasResult PointsToAA::aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L)
{
  if( !L )
    return MayAlias;

  if( !isa<PointerType>( P1.ptr->getType() ) )
    return MayAlias;
  if( !isa<PointerType>( P2.ptr->getType() ) )
    return MayAlias;

  const Ctx *ctx = read.getCtx(L);

  Ptrs ausA, ausB;
  if( !read.getUnderlyingAUs(P1.ptr,ctx,ausA) )
    return MayAlias;
  if( !read.getUnderlyingAUs(P2.ptr,ctx,ausB) )
    return MayAlias;

  // Do they share a common AU?
  for(Ptrs::const_iterator i=ausA.begin(), e=ausA.end(); i!=e; ++i)
  {
    const AU *au1 = i->au;
    if( au1->type == AU_Null )
      continue;

    for(Ptrs::const_iterator j=ausB.begin(), f=ausB.end(); j!=f; ++j)
    {
      const AU *au2 = j->au;
      if( au2->type == AU_Null )
        continue;

      if( (*au1) == (*au2) )
        return MayAlias;
    }
  }

  ++numNoAlias;
  return NoAlias;
}


}
}

