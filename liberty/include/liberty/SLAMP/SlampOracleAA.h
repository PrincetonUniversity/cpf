#ifndef LLVM_LIBERTY_SLAMP_SLAMP_ORACLE_AA_H
#define LLVM_LIBERTY_SLAMP_SLAMP_ORACLE_AA_H

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "SLAMPLoad.h"

namespace liberty
{

using namespace llvm;
using namespace slamp;

class SlampOracle : public LoopAA
{
public:
  //SlampOracle(SLAMPLoadProfile *l) : slamp(l) {}
  SlampOracle(SLAMPLoadProfile *l) : LoopAA(), slamp(l) {}
  //~SlampOracle() {}

  StringRef getLoopAAName() const { return "slamp-oracle-aa"; }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R, DesiredAliasResult dAliasRes = DNoOrMustAlias);

  ModRefResult modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L, Remedies &R);

  ModRefResult modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L, Remedies &R);

private:
  SLAMPLoadProfile *slamp;
};

}

#endif
