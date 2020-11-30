#ifndef LLVM_LIBERTY_SPICEREMED_H
#define LLVM_LIBERTY_SPICEREMED_H

#include "liberty/Analysis/KillFlow.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/LoopDominators.h"
#include "liberty/Speculation/Read.h"

#include "unordered_set"

namespace liberty {
using namespace llvm;

class SpiceRemedy : public Remedy {
public:
  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;

  StringRef getSpiceRemedyName() const {
    return "spice-remedy";
  }

  StringRef getRemedyName() const { return getSpiceRemedyName(); };

  bool isExpensive() {
    return false;
  }
};

class SpiceRemediator : public Remediator {
public:
  SpiceRemediator(ModuleLoops &ml, LoopAA *aa)
      : Remediator(), mloops(ml), loopAA(aa){}

  Remedies satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms);

  void setLoopPDG(PDG *loopPDG, Loop *L) {
    pdg = loopPDG;
    Function *f = L->getHeader()->getParent();
    pdt = &mloops.getAnalysis_PostDominatorTree(f);
    li = &mloops.getAnalysis_LoopInfo(f);
    se = &mloops.getAnalysis_ScalarEvolution(f);
  }

  StringRef getRemediatorName() const {
    return "spice-remediator";
  }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   DataDepType dataDepTy, const Loop *L);

private:
  PDG *pdg;
  ModuleLoops &mloops;
  LoopAA *loopAA;
  PostDominatorTree *pdt;
  LoopInfo *li;
  ScalarEvolution *se;

};

} // namespace liberty

#endif
