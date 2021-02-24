#define DEBUG_TYPE "spec-priv-points-to-aa"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/PointsToAA.h"
#include "scaf/Utilities/FindUnderlyingObjects.h"

#define DEFAULT_POINTS_TO_REMED_COST 10001

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;
using namespace llvm::noelle;

STATISTIC(numNoAlias, "Num no-alias / no-modref");

bool PointsToRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<PointsToRemedy> pointstoRhs =
      std::static_pointer_cast<PointsToRemedy>(rhs);
  if (this->ptr1 == pointstoRhs->ptr1)
    return this->ptr2 < pointstoRhs->ptr2;
  return this->ptr1 < pointstoRhs->ptr1;
}

LoopAA::AliasResult PointsToAA::aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L,
    Remedies &R, DesiredAliasResult dAliasRes)
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

  std::shared_ptr<PointsToRemedy> remedy =
      std::shared_ptr<PointsToRemedy>(new PointsToRemedy());
  remedy->cost = DEFAULT_POINTS_TO_REMED_COST;
  remedy->ptr1 = P1.ptr;
  remedy->ptr2 = P2.ptr;
  R.insert(remedy);

  ++numNoAlias;
  return NoAlias;
}


}
}

