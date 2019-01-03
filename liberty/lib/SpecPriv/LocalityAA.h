// This is an adaptor class from a heap assignment
// to the AA stack.  It applies disjoint heap reasoning
// as a separation AA.  When applied to a PDG, it
// removes edges which will be speculated and validated
// by code that uses that heap assignment.
#ifndef LIBERTY_SPEC_PRIV_LOCALITY_ORACLE_AA_H
#define LIBERTY_SPEC_PRIV_LOCALITY_ORACLE_AA_H

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/SpecPriv/Read.h"
#include "Classify.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

/// Adapts separation speculation to LoopAA.
struct LocalityAA : public ClassicLoopAA // Not a pass!
{
  LocalityAA(const Read &rd, const HeapAssignment &c) : ClassicLoopAA(), read(rd), asgn(c) {}

  StringRef getLoopAAName() const { return "spec-priv-locality-oracle-aa"; }

  virtual AliasResult aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L);

private:
  const Read &read;
  const HeapAssignment &asgn;
};

}
}

#endif

