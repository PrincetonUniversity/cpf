#ifndef LLVM_LIBERTY_DSMTX_TXIOAA_H
#define LLVM_LIBERTY_DSMTX_TXIOAA_H

#include "liberty/Analysis/LoopAA.h"
namespace liberty
{
using namespace llvm;

struct TXIOAA: public LoopAA // Not a pass!
{
  TXIOAA() : LoopAA(){}

  StringRef getLoopAAName() const { return "txio-aa"; }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Value *ptrB, unsigned sizeB, const Loop *L,
                      Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Instruction *B, const Loop *L, Remedies &R);

  bool isTXIOFcn(const Instruction *inst);

  LoopAA::SchedulingPreference getSchedulingPreference() const
  {
    return SchedulingPreference(Low - 10);
  }

};

}

#endif

