#ifndef LLVM_LIBERTY_SMTX_AA_H
#define LLVM_LIBERTY_SMTX_AA_H

#include "liberty/Analysis/LoopAA.h"
#include "liberty/LAMP/LAMPLoadProfile.h"

#include "liberty/Speculation/SmtxManager.h"

namespace liberty
{
namespace SpecPriv
{

using namespace llvm;

struct SmtxAA : public LoopAA // Not a pass!
{
    SmtxAA(SmtxSpeculationManager *man) : LoopAA(), smtxMan(man) {}

    virtual SchedulingPreference getSchedulingPreference() const {
      return SchedulingPreference(Bottom);
    }

    StringRef getLoopAAName() const { return "smtx-aa"; }

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

  private:
    SmtxSpeculationManager *smtxMan;
};

}
}

#endif

