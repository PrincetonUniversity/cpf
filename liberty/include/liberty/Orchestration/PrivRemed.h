#ifndef LLVM_LIBERTY_PRIVREMED_H
#define LLVM_LIBERTY_PRIVREMED_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Orchestration/Remediator.h"

namespace liberty {
using namespace llvm;

class PrivRemedy : public Remedy {
public:
  const StoreInst *storeI;

  enum PrivRemedType {
    Normal = 0, // PartialOverlap
    FullOverlap
  };

  PrivRemedType type;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;

  StringRef getPrivRemedyName() const {
    switch (type) {
    case Normal:
      return "priv-remedy";
      break;
    case FullOverlap:
      return "priv-full-overlap-remedy";
      break;
    }
  }

  //StringRef getRemedyName() const { return getPrivRemedyName(); };
  StringRef getRemedyName() const { return "priv-remedy"; };
};

class PrivRemediator : public Remediator {
public:
  PrivRemediator(ModuleLoops &ml, TargetLibraryInfo *tli)
      : Remediator(), mloops(ml), tli(tli) {}

  void setLoopPDG(PDG *loopPDG, Loop *L) {
    pdg = loopPDG;
    Function *f = L->getHeader()->getParent();
    pdt = &mloops.getAnalysis_PostDominatorTree(f);
    li = &mloops.getAnalysis_LoopInfo(f);
    se = &mloops.getAnalysis_ScalarEvolution(f);
  }

  StringRef getRemediatorName() const {
    return "priv-remediator";
  }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   DataDepType dataDepTy, const Loop *L);

private:
  PDG *pdg;
  ModuleLoops &mloops;
  TargetLibraryInfo *tli;
  PostDominatorTree *pdt;
  LoopInfo *li;
  ScalarEvolution *se;

  bool isPrivate(const Instruction *I);
};

} // namespace liberty

#endif
