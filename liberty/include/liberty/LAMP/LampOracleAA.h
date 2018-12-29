#ifndef LLVM_LIBERTY_LAMP_ORACLE_AA_H
#define LLVM_LIBERTY_LAMP_ORACLE_AA_H

#include "liberty/Analysis/LoopAA.h"
#include "LAMPLoadProfile.h"

namespace liberty
{
using namespace llvm;

class LampOracle : public LoopAA // Not a pass!
{
    LAMPLoadProfile *lamp;

  public:
    LampOracle(LAMPLoadProfile *l) : LoopAA(), lamp(l) {}

    StringRef getLoopAAName() const { return "lamp-oracle-aa"; }

    AliasResult alias(
      const Value *ptrA, unsigned sizeA,
      TemporalRelation rel,
      const Value *ptrB, unsigned sizeB,
      const Loop *L);

    ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Value *ptrB, unsigned sizeB,
      const Loop *L);

    ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Instruction *B,
      const Loop *L);
};

}

#endif

