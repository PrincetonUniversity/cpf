#ifndef LLVM_LIBERTY_LOADED_VALUE_PREDICTION_ORACLE_REMED_H
#define LLVM_LIBERTY_LOADED_VALUE_PREDICTION_ORACLE_REMED_H

#include "llvm/IR/Instructions.h"

#include "PDG.hpp"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/Orchestration/Remediator.h"

#include <unordered_set>

namespace liberty {
using namespace llvm;

class LoadedValuePredRemedy : public Remedy {
public:
  const Value *ptr; // pointer of loop-invariant load instruction
  bool write;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "invariant-value-pred-remedy"; };
};

class LoadedValuePredRemediator : public Remediator {
public:
  LoadedValuePredRemediator(PredictionSpeculation *ps, LoopAA *aa)
      : Remediator(), predspec(ps), loopAA(aa), WAWcollabDepsHandled(0),
        RAWcollabDepsHandled(0) {}

  StringRef getRemediatorName() const {
    return "invariant-value-pred-remediator";
  }

  Remedies satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms);

  const Value *getPtr(const Instruction *I, DataDepType dataDepTy);

  bool mustAliasFast(const Value *ptr1, const Value *ptr2,
                     const DataLayout &DL);

  bool mustAlias(const Value *ptr1, const Value *ptr2);

  bool isPredictablePtr(const Value *ptr, const DataLayout &DL);

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   DataDepType dataDepTy, const Loop *L);

private:
  PredictionSpeculation *predspec;
  LoopAA *loopAA;

  uint64_t WAWcollabDepsHandled;
  uint64_t RAWcollabDepsHandled;

  std::unordered_set<const Value *> predictableMemLocs;
  std::unordered_set<const Value *> nonPredictableMemLocs;
  std::unordered_map<const Value *, const Value *>
      mustAliasWithPredictableMemLocMap;
};

} // namespace liberty

#endif
