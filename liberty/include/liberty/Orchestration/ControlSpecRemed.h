#ifndef LLVM_LIBERTY_SPEC_PRIV_EDGE_COUNT_ORACLE_REMED_H
#define LLVM_LIBERTY_SPEC_PRIV_EDGE_COUNT_ORACLE_REMED_H

#include "llvm/IR/Instructions.h"

#include "noelle/core/PDG.hpp"
#include "scaf/Utilities/ControlSpeculation.h"
#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"

#include <set>
#include <unordered_map>
#include <unordered_set>

#define DEFAULT_CTRL_REMED_COST 45
#define EXPENSIVE_CTRL_REMED_COST 48

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;

class ControlSpecRemedy : public Remedy {
public:
  const Instruction *brI;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "ctrl-spec-remedy"; };
};

class ControlSpecRemediator : public Remediator {
public:
  typedef std::set<DGEdge<Value> *> EdgeSet;

  ControlSpecRemediator(ControlSpeculation *ctrlspec)
      : Remediator(), speculator(ctrlspec) {}

  StringRef getRemediatorName() const { return "ctrl-spec-remediator"; }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool LoopCarried,
                   DataDepType dataDepTy, const Loop *L);

  RemedResp ctrldep(const Instruction *A, const Instruction *B, const Loop *L);

  RemedResp regdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   const Loop *L);

  void processLoopOfInterest(Loop *l);

  // void buildTransitiveIntraIterationControlDependenceCache(EdgeSet &cache);

private:
  ControlSpeculation *speculator;
  std::unordered_map<const Instruction *,
                     std::unordered_set<const Instruction *>>
      unremovableCtrlDeps;
  Loop *loop;
};

} // namespace liberty

#endif
