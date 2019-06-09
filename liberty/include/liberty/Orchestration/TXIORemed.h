#ifndef LLVM_LIBERTY_TXIOREMED_H
#define LLVM_LIBERTY_TXIOREMED_H

#include "liberty/Orchestration/Remediator.h"

namespace liberty
{
using namespace llvm;

class TXIORemedy : public Remedy {
public:
  const Instruction *printI;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "txio-remedy"; };
};

class TXIORemediator : public Remediator {
public:
  StringRef getRemediatorName() const { return "txio-remediator"; }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   bool RAW, const Loop *L);

  static bool isTXIOFcn(const Instruction *inst);
};

} // namespace liberty

#endif

