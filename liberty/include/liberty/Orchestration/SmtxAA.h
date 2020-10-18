#ifndef LLVM_LIBERTY_SMTX_AA_H
#define LLVM_LIBERTY_SMTX_AA_H

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Speculation/SmtxManager.h"

namespace liberty {
namespace SpecPriv {

using namespace llvm;

class SmtxLampRemedy : public Remedy {
public:
  const Instruction *writeI;
  const Instruction *readI;
  const Instruction *memI;

  // void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  unsigned long setCost(PerformanceEstimator *perf);
  StringRef getRemedyName() const { return "smtx-lamp-remedy"; };

  bool isExpensive() { return true; }
};

struct SmtxAA : public LoopAA // Not a pass!
{
  SmtxAA(SmtxSpeculationManager *man, PerformanceEstimator *pf)
      : LoopAA(), smtxMan(man), perf(pf) {}

  virtual SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Bottom + 1);
  }

  StringRef getLoopAAName() const { return "smtx-aa"; }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R, DesiredAliasResult dAliasRes = DNoOrMustAlias);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Value *ptrB, unsigned sizeB, const Loop *L,
                      Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Instruction *B, const Loop *L, Remedies &R);

private:
  SmtxSpeculationManager *smtxMan;
  PerformanceEstimator *perf;
};
} // namespace SpecPriv
} // namespace liberty

#endif

