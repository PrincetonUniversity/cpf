#ifndef LLVM_LIBERTY_LOOP_FISSION_REMED_H
#define LLVM_LIBERTY_LOOP_FISSION_REMED_H

#include "liberty/Orchestration/Remediator.h"

#include "LoopDependenceInfo.hpp"
#include "PDG.hpp"
#include "SCCDAG.hpp"

#include <unordered_set>
#include <queue>

namespace liberty {
using namespace llvm;

class LoopFissionRemedy : public Remedy {
public:
  const SCC *seqSCC;

  void apply(PDG &pdg);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "loop-fission-remedy"; };
};

class LoopFissionRemediator : public Remediator {
public:
  LoopFissionRemediator(PDG &pdg, LoopDependenceInfo *ldi)
      : Remediator(), loopDepInfo(ldi) {}

  StringRef getRemediatorName() const { return "loop-fission-remediator"; }

  RemedResp ctrldep(const Instruction *A, const Instruction *B, const Loop *L);

  RemedResp regdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   const Loop *L);

private:
  std::unordered_set<SCC*> replicableSCCs;
  std::unordered_set<SCC*> nonReplicableSCCs;
  //const SCCDAG *sccdag;
  LoopDependenceInfo *loopDepInfo;

  bool seqStageEligible(SCCDAG *sccdag, std::queue<SCC *> sccQ,
                        std::unordered_set<SCC *> visited);

  RemedResp removeDep(const Instruction *A, const Instruction *B,
                      bool LoopCarried);

  bool isReplicable(SCC *scc);
};

} // namespace liberty

#endif
