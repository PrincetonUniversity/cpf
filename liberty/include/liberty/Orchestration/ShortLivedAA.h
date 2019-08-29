// This is an adaptor class from a short-lived heap assignment
// to the AA stack.  It applies disjoint heap reasoning
// as a separation AA.  When applied to a PDG, it
// removes edges which will be speculated and validated
// by code that uses that heap assignment.
#ifndef LIBERTY_SPEC_PRIV_SHORT_LIVED_AA_H
#define LIBERTY_SPEC_PRIV_SHORT_LIVED_AA_H

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Classify.h"

namespace liberty
{
using namespace llvm;
using namespace SpecPriv;

struct ShortLivedAA : public ClassicLoopAA // Not a pass!
{
  ShortLivedAA(const Read &rd, const HeapAssignment &ha, const Ctx *cx)
      : ClassicLoopAA(), read(rd), asgn(ha), ctx(cx) {}

  StringRef getLoopAAName() const { return "spec-priv-local-aa"; }

  virtual AliasResult aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L);

private:
  const Read &read;
  const HeapAssignment &asgn;
  const Ctx *ctx;
};

}

#endif

