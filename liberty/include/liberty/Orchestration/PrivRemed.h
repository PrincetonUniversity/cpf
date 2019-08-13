#ifndef LLVM_LIBERTY_PRIVREMED_H
#define LLVM_LIBERTY_PRIVREMED_H

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

class PrivRemedy : public Remedy {
public:
  const StoreInst *storeI;
  const Value *localPtr;
  bool ctrlSpecUsed;

  enum PrivRemedType {
    Normal = 0, // PartialOverlap
    FullOverlap,
    Local
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
    case Local:
      return "priv-local-remedy";
      break;
    }
  }

  StringRef getRemedyName() const { return getPrivRemedyName(); };
  //StringRef getRemedyName() const { return "priv-remedy"; };
};

class PrivRemediator : public Remediator {
public:
  PrivRemediator(ModuleLoops &ml, TargetLibraryInfo *tli, LoopAA *aa,
                 ControlSpeculation *cs, KillFlow &kill, const Read &rd,
                 const HeapAssignment &c)
      : Remediator(), mloops(ml), tli(tli), loopAA(aa), cs(cs), killFlow(kill),
        read(rd), asgn(c), collabDepsHandled(0) {}

  Remedies satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms);

  void setLoopPDG(PDG *loopPDG, Loop *L) {
    pdg = loopPDG;
    Function *f = L->getHeader()->getParent();
    pdt = &mloops.getAnalysis_PostDominatorTree(f);
    li = &mloops.getAnalysis_LoopInfo(f);
    se = &mloops.getAnalysis_ScalarEvolution(f);
    cs->setLoopOfInterest(L->getHeader());
    specDT = std::make_unique<LoopDom>(*cs, L);
    noSpecDT = std::make_unique<LoopDom>(nocs, L);
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
  LoopAA *loopAA;
  PostDominatorTree *pdt;
  LoopInfo *li;
  ScalarEvolution *se;
  ControlSpeculation *cs;
  KillFlow &killFlow;
  const Read &read;
  const HeapAssignment &asgn;
  NoControlSpeculation nocs;
  std::unique_ptr<LoopDom> specDT;
  std::unique_ptr<LoopDom> noSpecDT;

  uint64_t collabDepsHandled;

  std::unordered_set<const GlobalValue *> localGVs;
  std::unordered_set<const AllocaInst *> localAllocas;

  typedef std::pair<const BasicBlock *, const Value *> BBPtrPair;
  typedef DenseMap<BBPtrPair, bool> BBKills;
  // we can summarize BBs in terms of which values they kill
  BBKills bbKills;

  typedef std::pair<const Value*, const Value *> PtrsPair;
  typedef DenseMap<PtrsPair, bool> PtrsMustAlias;
  PtrsMustAlias ptrsMustAlias;

  bool isPrivate(const Instruction *I, const Loop *L, bool &ctrlSpecUsed);
  bool isLocalPrivate(const Instruction *I, const Value *ptr,
                      DataDepType dataDepTy, const Loop *L, bool &ctrlSpecUsed);

  bool mustAlias(const Value *ptr1, const Value *ptr2);
  bool instMustKill(const Instruction *inst, const Value *ptr, const Loop *L);
  bool blockMustKill(const BasicBlock *bb, const Value *ptr,
                     const Instruction *before, const Loop *L);
  bool isPointerKillBefore(const Loop *L, const Value *ptr,
                           const Instruction *before, bool useCtrlSpec = true);
  bool isSpecSeparated(const Instruction *I1, const Instruction *I2,
                       const Loop *L);
};

} // namespace liberty

#endif
