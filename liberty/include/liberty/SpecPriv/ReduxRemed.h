#ifndef LLVM_LIBERTY_REDUX_REMED_H
#define LLVM_LIBERTY_REDUX_REMED_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/SpecPriv/Remediator.h"

#include "liberty/SpecPriv/Reduction.h"
#include "liberty/Utilities/ModuleLoops.h"

#include "LoopDependenceInfo.hpp"

#include <unordered_set>

namespace liberty
{

using namespace llvm;
using namespace SpecPriv;

class ReduxRemedy : public Remedy {
public:
  const Instruction *reduxI;
  const SCC *reduxSCC;

  void apply(llvm::PDG &pdg);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "redux-remedy"; };
};

class ReduxRemediator : public Remediator {
public:
  ReduxRemediator(ModuleLoops *ml, LoopDependenceInfo *ldi)
      : Remediator(), mloops(ml), loopDepInfo(ldi) {}

  void setLoopOfInterest(Loop *l) {
    Function *f = l->getHeader()->getParent();
    se = &mloops->getAnalysis_ScalarEvolution(f);
    // clear the cached results for the new loop
    regReductions.clear();
  }

  StringRef getRemediatorName() const { return "redux-remediator"; }

  RemedResp regdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   const Loop *L);

  bool isRegReductionPHI(Instruction *I, Loop *l);

private:
  std::unordered_set<const Instruction *> regReductions;
  ModuleLoops *mloops;
  ScalarEvolution *se;
  LoopDependenceInfo *loopDepInfo;
};

} // namespace liberty

#endif
