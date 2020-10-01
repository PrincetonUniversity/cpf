// This is an adaptor class from PtrResidueSpeculationManager
// to the AA stack.
// It reasons about pointer-residues to give
// no-alias results.
#ifndef LIBERTY_SPEC_PRIV_PTR_RESIDUE_ORACLE_AA_H
#define LIBERTY_SPEC_PRIV_PTR_RESIDUE_ORACLE_AA_H

#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/PtrResidueManager.h"
#include "liberty/Strategy/PerformanceEstimator.h"
#include "liberty/Orchestration/Remediator.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

class PtrResidueRemedy : public Remedy {
public:
  const Value *ptr1;
  const Ctx *ctx1;
  const Value *ptr2;
  const Ctx *ctx2;

  const Value *ptr;
  const Ctx *ctx;

  //void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  unsigned long setCost(PerformanceEstimator *perf, const Value *ptr);
  StringRef getRemedyName() const { return "ptr-residue-remedy"; };
};

struct PtrResidueAA : public LoopAA // Not a pass!
{
  PtrResidueAA(const DataLayout &TD, PtrResidueSpeculationManager &man,
               PerformanceEstimator *pf)
      : LoopAA(), td(TD), manager(man), perf(pf) {}

  virtual SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Low - 4);
  }

  StringRef getLoopAAName() const { return "spec-priv-ptr-residue-aa"; }

  virtual AliasResult alias(const Value *P1, unsigned S1, TemporalRelation rel,
                            const Value *P2, unsigned S2, const Loop *L,
                            Remedies &R,
                            DesiredAliasResult dAliasRes = DNoOrMustAlias);

  virtual ModRefResult modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L, Remedies &R);

  virtual ModRefResult modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L, Remedies &R);

private:
  const DataLayout &td;
  PtrResidueSpeculationManager &manager;
  PerformanceEstimator *perf;

  /// Can there be an alias?  If so, report necessary assumptions
  bool may_alias(
    const Value *P1, unsigned S1,
    TemporalRelation rel,
    const Value *P2, unsigned S2,
    const Loop *L,
    PtrResidueSpeculationManager::Assumption &a1_out,
    PtrResidueSpeculationManager::Assumption &a2_out) const;

  /// Can there be a mod-ref?  If so, report necessary assumptions
  bool may_modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L,
    PtrResidueSpeculationManager::Assumption &a1_out,
    PtrResidueSpeculationManager::Assumption &a2_out) const;
};

}
}

#endif

