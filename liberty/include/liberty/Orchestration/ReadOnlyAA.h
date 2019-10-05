// This is an adaptor class from a read-only heap assignment
// to the AA stack.  It applies disjoint heap reasoning
// as a separation AA.  When applied to a PDG, it
// removes edges which will be speculated and validated
// by code that uses that heap assignment.
#ifndef LIBERTY_SPEC_PRIV_READ_ONLY_AA_H
#define LIBERTY_SPEC_PRIV_READ_ONLY_AA_H

//#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Classify.h"

namespace liberty
{
using namespace llvm;
using namespace SpecPriv;

//struct ReadOnlyAA : public ClassicLoopAA // Not a pass!
struct ReadOnlyAA : public LoopAA // Not a pass!
{
  ReadOnlyAA(const Read &rd, const HeapAssignment &ha, const Ctx *cx)
      : LoopAA(), read(rd), asgn(ha), ctx(cx) {}
      //: ClassicLoopAA(), read(rd), asgn(ha), ctx(cx) {}

  virtual SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Bottom + 4);
  }

  StringRef getLoopAAName() const { return "spec-priv-read-only-aa"; }

  LoopAA::AliasResult alias(const Value *ptrA, unsigned sizeA,
                            TemporalRelation rel, const Value *ptrB,
                            unsigned sizeB, const Loop *L, Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Value *ptrB, unsigned sizeB, const Loop *L,
                      Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Instruction *B, const Loop *L, Remedies &R);

  /*
  virtual AliasResult aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L);
  */

private:
  const Read &read;
  const HeapAssignment &asgn;
  const Ctx *ctx;

  LoopAA::ModRefResult check_modref(const Value *ptrA, const Value *ptrB,
                                    const Loop *L);
};

}

#endif

