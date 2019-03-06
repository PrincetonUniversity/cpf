#ifndef LLVM_LIBERTY_LOOP_FISSION_REMED_H
#define LLVM_LIBERTY_LOOP_FISSION_REMED_H

#include "liberty/Orchestration/Remediator.h"

#include "PDG.hpp"

#include <unordered_set>
#include <queue>

namespace liberty {
using namespace llvm;

class LoopFissionRemedy : public Remedy {
public:
  const Instruction *produceI;
  std::unordered_set<const Instruction *> replicatedI;

  void apply(PDG &pdg);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "loop-fission-remedy"; };
};

class LoopFissionRemediator : public Remediator {
public:
  LoopFissionRemediator(PDG *pdg) : Remediator(), pdg(pdg) {}

  StringRef getRemediatorName() const { return "loop-fission-remediator"; }

  RemedCriticResp removeCtrldep(const Instruction *A, const Instruction *B,
                                const Loop *L);

  RemedCriticResp removeRegDep(const Instruction *A, const Instruction *B,
                               bool loopCarried, const Loop *L);

  RemedCriticResp satisfy(Loop *loop, const Criticism *cr);

private:
  PDG *pdg;

  bool seqStageEligible(std::queue<const Instruction *> instQ,
                        std::unordered_set<const Instruction *> visited,
                        Criticisms &cr);

  RemedCriticResp removeDep(const Instruction *A, const Instruction *B,
                            bool LoopCarried);
};

} // namespace liberty

#endif
