#ifndef LLVM_LIBERTY_SMTX_SLAMP_REMED_H
#define LLVM_LIBERTY_SMTX_SLAMP_REMED_H

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/QueryCacheing.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Speculation/SmtxSlampManager.h"
#include "liberty/SLAMP/SLAMPLoad.h"

#include <set>

namespace liberty
{
using namespace llvm;

class SmtxSlampRemedy : public Remedy {
public:
  const Instruction *writeI;
  const Instruction *readI;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "smtx-remedy"; };
};

class SmtxSlampRemediator : public Remediator {
public:
  SmtxSlampRemediator(SpecPriv::SmtxSlampSpeculationManager *man)
      : Remediator(), smtxMan(man) {}

  StringRef getRemediatorName() const { return "smtx-remediator"; }

  /*
  void queryAcrossCallsites(
    const Instruction* A,
    LoopAA::TemporalRelation rel,
    const Instruction* B,
    const Loop *L);
  */

  RemedResp memdep(const Instruction *A, const Instruction *B,
                   bool LoopCarried, bool RAW, const Loop *L);

private:
  // TODO: eventually remove this manager
  SpecPriv::SmtxSlampSpeculationManager *smtxMan;
  DenseMap<IIKey, bool> queried;
};

} // namespace liberty

#endif

