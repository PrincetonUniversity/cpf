#ifndef LLVM_LIBERTY_SMTX_LAMP_REMED_H
#define LLVM_LIBERTY_SMTX_LAMP_REMED_H

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/QueryCacheing.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Speculation/SmtxManager.h"
#include "liberty/LAMP/LAMPLoadProfile.h"

#include <set>

namespace liberty
{
using namespace llvm;

class SmtxLampRemedy : public Remedy {
public:
  const Instruction *writeI;
  const Instruction *readI;

  void apply(PDG &pdg);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "smtx-lamp-remedy"; };
};

class SmtxLampRemediator : public Remediator {
public:
  SmtxLampRemediator(SpecPriv::SmtxSpeculationManager *man)
      : Remediator(), smtxMan(man) {}

  StringRef getRemediatorName() const { return "smtx-lamp-remediator"; }

  RemedResp memdep(const Instruction *A, const Instruction *B,
                   bool LoopCarried, bool RAW, const Loop *L);

private:
  // TODO: eventually remove this manager
  SpecPriv::SmtxSpeculationManager *smtxMan;
  DenseMap<IIKey, bool> queried;
};

} // namespace liberty

#endif
