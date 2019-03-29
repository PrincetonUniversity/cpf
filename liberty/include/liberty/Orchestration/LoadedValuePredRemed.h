#ifndef LLVM_LIBERTY_LOADED_VALUE_PREDICTION_ORACLE_REMED_H
#define LLVM_LIBERTY_LOADED_VALUE_PREDICTION_ORACLE_REMED_H

#include "llvm/IR/Instructions.h"

#include "PDG.hpp"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/Orchestration/Remediator.h"

namespace liberty {
using namespace llvm;

class LoadedValuePredRemedy : public Remedy {
public:
  const LoadInst *loadI; // loop-invariant load instruction

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "loaded-value-pred-remedy"; };
};

class LoadedValuePredRemediator : public Remediator {
public:
  LoadedValuePredRemediator(PredictionSpeculation *ps)
      : Remediator(), predspec(ps) {}

  StringRef getRemediatorName() const { return "loaded-value-pred-remediator"; }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   bool RAW, const Loop *L);

private:
  PredictionSpeculation *predspec;
};

} // namespace liberty

#endif
