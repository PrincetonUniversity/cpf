#ifndef LLVM_LIBERTY_SMTX_SLAMP_AA_H
#define LLVM_LIBERTY_SMTX_SLAMP_AA_H

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/QueryCacheing.h"
#include "liberty/SLAMP/SLAMPLoad.h"

#include "liberty/SpecPriv/SmtxSlampManager.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{

using namespace llvm;

struct SmtxSlampAA : public LoopAA // Not a pass!
{
    SmtxSlampAA(SmtxSlampSpeculationManager *man) : LoopAA(), smtxMan(man)
    { }

    StringRef getLoopAAName() const { return "smtx-aa"; }

    void queryAcrossCallsites(
      const Instruction* A,
      TemporalRelation rel,
      const Instruction* B,
      const Loop *L);

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

    LoopAA::SchedulingPreference getSchedulingPreference() const
    {
      return SchedulingPreference(Bottom + 2);
    }

  private:
    SmtxSlampSpeculationManager* smtxMan;
    DenseMap<IIKey, bool>        queried;
};

}
}

#endif

