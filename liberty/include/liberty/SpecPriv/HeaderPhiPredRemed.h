#ifndef LLVM_LIBERTY_HEADER_PHI_PREDICTION_ORACLE_REMED_H
#define LLVM_LIBERTY_HEADER_PHI_PREDICTION_ORACLE_REMED_H

#include "llvm/IR/Instructions.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/SpecPriv/Remediator.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "PDG.hpp"

namespace liberty {
using namespace llvm;

class HeaderPhiPredRemedy : public Remedy {
public:
  const PHINode *predPHI;
  const LoadInst *loadI; // populated for loop-invariant loads that feed
                         // header-phi nodes

  void apply(PDG &pdg);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "header-phi-pred-remedy"; };
};

class HeaderPhiPredRemediator : public Remediator {
public:
  HeaderPhiPredRemediator(PredictionSpeculation *ps)
      : Remediator(), predspec(ps) {}

  StringRef getRemediatorName() const { return "header-phi-pred-remediator"; }

  RemedResp regdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   const Loop *L);

private:
  PredictionSpeculation *predspec;
};

} // namespace liberty

#endif
