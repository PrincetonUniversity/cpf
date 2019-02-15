// An adaptor between the underlying-object instrumentation
// and the loop AA stack.  Can be used in place of LAMP;
// haven't checked how good that might be...
#ifndef LIBERTY_SPEC_PRIV_POINTS_TO_ORACLE_AA_H
#define LIBERTY_SPEC_PRIV_POINTS_TO_ORACLE_AA_H

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Speculation/Read.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

// You can use it as a LoopAA too!
struct PointsToAA : public ClassicLoopAA // Not a pass!
{
  PointsToAA(const Read &rd) : ClassicLoopAA(), read(rd) {}

  StringRef getLoopAAName() const { return "spec-priv-points-to-oracle-aa"; }

  virtual AliasResult aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L);

private:
  const Read &read;
};

}
}

#endif

