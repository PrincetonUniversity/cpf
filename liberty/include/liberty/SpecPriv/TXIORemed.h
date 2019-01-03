#ifndef LLVM_LIBERTY_TXIOREMED_H
#define LLVM_LIBERTY_TXIOREMED_H

#include "liberty/SpecPriv/Remediator.h"

namespace liberty
{
using namespace llvm;

class TXIORemedy : public Remedy {
public:
  const Instruction *printI;

  void apply(PDG &pdg);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "txio-remedy"; };
};

class TXIORemediator : public Remediator {
public:
  StringRef getRemediatorName() const { return "txio-remediator"; }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   const Loop *L);

  bool isTXIOFcn(const Instruction *inst);
};

} // namespace liberty

#endif

