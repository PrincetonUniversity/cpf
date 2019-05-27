#ifndef LLVM_LIBERTY_REDUX_REMED_H
#define LLVM_LIBERTY_REDUX_REMED_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Redux/Reduction.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Analysis/ReductionDetection.h"

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

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "redux-remedy"; };
};

class ReduxRemediator : public Remediator {
public:
  ReduxRemediator(ModuleLoops *ml, LoopDependenceInfo *ldi, LoopAA *aa,
                  PDG *lpdg)
      : Remediator(), mloops(ml), loopDepInfo(ldi), loopAA(aa), pdg(lpdg) {}

  void setLoopOfInterest(Loop *l) {
    Function *f = l->getHeader()->getParent();
    se = &mloops->getAnalysis_ScalarEvolution(f);
    // clear the cached results for the new loop
    regReductions.clear();
    memReductions.clear();
    findMemReductions(l);
    findMinMaxRegReductions(l);
  }

  StringRef getRemediatorName() const { return "redux-remediator"; }

  void findMemReductions(Loop *l);
  void findMinMaxRegReductions(Loop *l);

  RemedResp regdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   const Loop *L);

  RemedResp memdep(const Instruction *A, const Instruction *B, bool LoopCarried,
                   bool RAW, const Loop *L);

  bool isRegReductionPHI(Instruction *I, Loop *l);
  bool isConditionalReductionPHI(const Instruction *I, const Loop *l) const;
  bool isMemReduction(const Instruction *I);

private:
  std::unordered_set<const Instruction *> regReductions;
  std::unordered_set<const StoreInst*> memReductions;
  ModuleLoops *mloops;
  ScalarEvolution *se;
  LoopDependenceInfo *loopDepInfo;
  LoopAA *loopAA;
  PDG *pdg;
  ReductionDetection reduxdet;
};

} // namespace liberty

#endif
