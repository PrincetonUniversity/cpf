#ifndef LLVM_LIBERTY_DSMTX_TXIOAA_H
#define LLVM_LIBERTY_DSMTX_TXIOAA_H

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"

namespace liberty
{
using namespace llvm;

class TXIORemedy : public Remedy {
public:
  const Instruction *printI;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "txio-remedy"; };
};

struct TXIOAA: public LoopAA // Not a pass!
{
  TXIOAA() : LoopAA(){}

  StringRef getLoopAAName() const { return "txio-aa"; }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R, DesiredAliasResult dAliasRes = DNoOrMustAlias);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Value *ptrB, unsigned sizeB, const Loop *L,
                      Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Instruction *B, const Loop *L, Remedies &R);

  static bool isTXIOFcn(const Instruction *inst);

  LoopAA::SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Low - 9);
  }
};

}

#endif

