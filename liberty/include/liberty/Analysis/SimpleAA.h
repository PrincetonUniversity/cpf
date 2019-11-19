#ifndef LLVM_LIBERTY_SIMPLEAA_H
#define LLVM_LIBERTY_SIMPLEAA_H

#include "liberty/Analysis/LoopAA.h"
namespace liberty {
using namespace llvm;

struct SimpleAA : public LoopAA // Not a pass!
{
  SimpleAA() : LoopAA() {}

  StringRef getLoopAAName() const { return "simple-aa"; }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Value *ptrB, unsigned sizeB, const Loop *L,
                      Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Instruction *B, const Loop *L, Remedies &R);

  LoopAA::SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Normal - 10);
  }
};

} // namespace liberty

#endif
