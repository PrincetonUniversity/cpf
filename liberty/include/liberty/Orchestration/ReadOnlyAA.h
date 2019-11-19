// This is an adaptor class from a read-only heap assignment
// to the AA stack.  It applies disjoint heap reasoning
// as a separation AA.  When applied to a PDG, it
// removes edges which will be speculated and validated
// by code that uses that heap assignment.
#ifndef LIBERTY_SPEC_PRIV_READ_ONLY_AA_H
#define LIBERTY_SPEC_PRIV_READ_ONLY_AA_H

//#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Read.h"

namespace liberty
{
using namespace llvm;
using namespace SpecPriv;

//struct ReadOnlyAA : public ClassicLoopAA // Not a pass!
struct ReadOnlyAA : public LoopAA, Remediator // Not a pass!
{
  ReadOnlyAA(const Read &rd, const HeapAssignment &ha, const Ctx *cx)
      : LoopAA(), read(rd), asgn(&ha), readOnlyAUs(nullptr), ctx(cx) {}
  //: ClassicLoopAA(), read(rd), asgn(ha), ctx(cx) {}

  ReadOnlyAA(const Read &rd, const HeapAssignment::AUSet *roAUs, const Ctx *cx)
      : LoopAA(), read(rd), asgn(nullptr), readOnlyAUs(roAUs), ctx(cx) {}

  virtual SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Bottom + 9);
  }

  StringRef getLoopAAName() const { return "spec-priv-read-only-aa"; }

  StringRef getRemediatorName() const { return "spec-priv-read-only-remed"; }

  LoopAA::AliasResult alias(const Value *ptrA, unsigned sizeA,
                            TemporalRelation rel, const Value *ptrB,
                            unsigned sizeB, const Loop *L, Remedies &R);

  LoopAA::ModRefResult modref(const Instruction *A, TemporalRelation rel,
                              const Value *ptrB, unsigned sizeB, const Loop *L,
                              Remedies &R);

  LoopAA::ModRefResult modref(const Instruction *A, TemporalRelation rel,
                              const Instruction *B, const Loop *L, Remedies &R);

  /*
  virtual AliasResult aliasCheck(
    const Pointer &P1,
    TemporalRelation rel,
    const Pointer &P2,
    const Loop *L);
  */

  LoopAA::ModRefResult modref_with_ptrs(const Instruction *A, const Value *ptrA,
                                        TemporalRelation rel,
                                        const Instruction *B, const Value *ptrB,
                                        const Loop *L, Remedies &R);

private:
  const Read &read;
  const HeapAssignment *asgn;
  const HeapAssignment::AUSet *readOnlyAUs;
  const Ctx *ctx;

  LoopAA::ModRefResult check_modref(const Value *ptrA, const Value *ptrB,
                                    const Loop *L, Remedies &R);
};

}

#endif

