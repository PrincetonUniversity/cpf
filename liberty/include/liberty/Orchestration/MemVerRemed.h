#ifndef LLVM_LIBERTY_MEM_VER_REMED_H
#define LLVM_LIBERTY_MEM_VER_REMED_H

#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Orchestration/Remediator.h"

namespace liberty {
using namespace llvm;

class MemVerRemedy : public Remedy {
public:
  bool waw;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  unsigned long getCost(PerformanceEstimator *perf) const;
  StringRef getRemedyName() const { return "mem-ver-remedy"; };
};

class MemVerRemediator : public Remediator {
public:
  StringRef getRemediatorName() const { return "mem-ver-remediator"; }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   DataDepType dataDepTy, const Loop *L);
};

} // namespace liberty

#endif
