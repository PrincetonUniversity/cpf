#ifndef LLVM_LIBERTY_COUNTED_IV_REMED_H
#define LLVM_LIBERTY_COUNTED_IV_REMED_H

#include "llvm/IR/Instructions.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "PDG.hpp"

namespace liberty {
using namespace llvm;

class CountedIVRemedy : public Remedy {
public:
  const SCC *ivSCC;
  const PHINode *ivPHI;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "counted-iv-remedy"; };
};

class CountedIVRemediator : public Remediator {
public:
  CountedIVRemediator(LoopDependenceInfo *ldi)
      : Remediator(), loopDepInfo(ldi) {}

  StringRef getRemediatorName() const { return "counted-iv-remediator"; }

  RemedResp regdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   const Loop *L);

  RemedResp ctrldep(const Instruction *A, const Instruction *B, const Loop *L);

private:
  LoopDependenceInfo *loopDepInfo;
};

} // namespace liberty

#endif
