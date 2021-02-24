// An adaptor between the underlying-object instrumentation
// and the loop AA stack.  Can be used in place of LAMP;
// haven't checked how good that might be...
#ifndef LIBERTY_SPEC_PRIV_POINTS_TO_ORACLE_AA_H
#define LIBERTY_SPEC_PRIV_POINTS_TO_ORACLE_AA_H

#include "scaf/MemoryAnalysisModules/ClassicLoopAA.h"
#include "liberty/Speculation/Read.h"

#include "Assumptions.h"

namespace liberty {
namespace SpecPriv {
using namespace llvm;
using namespace llvm::noelle;

class PointsToRemedy : public Remedy {
public:
  const Value *ptr1;
  const Value *ptr2;

  // void apply(Task *task) {};
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "points-to-remedy"; };

  bool isExpensive() { return true; }
};

// You can use it as a LoopAA too!
struct PointsToAA : public ClassicLoopAA // Not a pass!
{
  PointsToAA(const Read &rd) : ClassicLoopAA(), read(rd) {}

  virtual SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Bottom);
  }

  StringRef getLoopAAName() const { return "spec-priv-points-to-oracle-aa"; }

  virtual AliasResult aliasCheck(const Pointer &P1, TemporalRelation rel,
                                 const Pointer &P2, const Loop *L, Remedies &R,
                                 DesiredAliasResult dAliasRes = DNoOrMustAlias);

private:
  const Read &read;
};

} // namespace SpecPriv
} // namespace liberty

#endif

