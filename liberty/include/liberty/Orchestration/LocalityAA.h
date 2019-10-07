// This is an adaptor class from a heap assignment
// to the AA stack.  It applies disjoint heap reasoning
// as a separation AA.  When applied to a PDG, it
// removes edges which will be speculated and validated
// by code that uses that heap assignment.
#ifndef LIBERTY_SPEC_PRIV_LOCALITY_ORACLE_AA_H
#define LIBERTY_SPEC_PRIV_LOCALITY_ORACLE_AA_H

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Classify.h"

namespace liberty
{
using namespace llvm;
using namespace SpecPriv;

/// Adapts separation speculation to LoopAA.
struct LocalityAA : public ClassicLoopAA // Not a pass!
{
  LocalityAA(const Read &rd, const HeapAssignment &ha, const Ctx *cx)
      : ClassicLoopAA(), read(rd), asgn(ha), ctx(cx) {}

  StringRef getLoopAAName() const { return "spec-priv-locality-oracle-aa"; }

  virtual AliasResult aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L,
    Remedies &R);

  virtual ModRefResult modref(const Instruction *I1, TemporalRelation Rel,
                              const Instruction *I2, const Loop *L,
                              Remedies &remeds);

private:
  const Read &read;
  const HeapAssignment &asgn;
  const Ctx *ctx;

  unordered_set<const Value*> privateInsts;
};

}

#endif

