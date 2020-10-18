#ifndef LIBERTY_PRIV_AA_H
#define LIBERTY_PRIV_AA_H

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Read.h"

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

class PrivRemedy : public Remedy {
public:
  // const StoreInst *storeI;
  const Value *privPtr;
  const Value *altPrivPtr;
  const Value *localPtr;
  bool ctrlSpecUsed;

  enum PrivRemedType {
    Normal = 0, // PartialOverlap
    FullOverlap,
    Local
  };

  PrivRemedType type;

  // void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;

  StringRef getPrivRemedyName() const {
    return "priv-remedy";
    /*switch (type) {
    case Normal:
      return "priv-remedy";
      break;
    case FullOverlap:
      return "priv-full-overlap-remedy";
      break;
    case Local:
      return "priv-local-remedy";
      break;
    default:
      assert(false && "No priv-remedy type?");
    }*/
  }

  StringRef getRemedyName() const { return getPrivRemedyName(); };

  bool isExpensive() {
    if (type == Normal)
      return true;
    else
      return false;
  }
};

/// Adapts separation speculation to LoopAA.
struct PrivAA : public LoopAA // Not a pass!
{
  PrivAA(const Read &rd, const HeapAssignment &ha, const Ctx *cx,
         KillFlow &kill, ModuleLoops &ml, Loop *L)
      : LoopAA(), read(rd), asgn(ha), ctx(cx), killFlow(kill), mloops(ml) {
    Function *f = L->getHeader()->getParent();
    pdt = &mloops.getAnalysis_PostDominatorTree(f);
    li = &mloops.getAnalysis_LoopInfo(f);
    se = &mloops.getAnalysis_ScalarEvolution(f);
    usedCheapPrivPtrs.clear();
  }

  StringRef getLoopAAName() const { return "priv-aa"; }

  LoopAA::AliasResult alias(const Value *P1, unsigned S1, TemporalRelation rel,
                            const Value *P2, unsigned S2, const Loop *L,
                            Remedies &R,
                            DesiredAliasResult dAliasRes = DNoOrMustAlias);

  LoopAA::ModRefResult modref(const Instruction *A, TemporalRelation rel,
                              const Value *ptrB, unsigned sizeB, const Loop *L,
                              Remedies &R);

  LoopAA::ModRefResult modref(const Instruction *I1, TemporalRelation Rel,
                              const Instruction *I2, const Loop *L,
                              Remedies &remeds);

  LoopAA::SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Bottom + 6);
  }

private:
  const Read &read;
  const HeapAssignment &asgn;
  const Ctx *ctx;
  KillFlow &killFlow;
  ModuleLoops &mloops;
  PostDominatorTree *pdt;
  LoopInfo *li;
  ScalarEvolution *se;

  std::unordered_set<const Value *> usedCheapPrivPtrs;
  std::unordered_set<const AU *> usedFullOverlapPrivAUs;

  bool isCheapPrivate(const Instruction *I, const Value **ptr, const Loop *L,
                      Remedies &R, Ptrs &aus);

  bool hasUsedFullOverlapPrivAUs(const Ptrs &aus);
  BasicBlock *getLoopEntryBB(const Loop *loop);
  bool isTransLoopInvariant(const Value *val, const Loop *L);
  bool isLoopInvariantValue(const Value *V, const Loop *L);
  bool extractValuesInSCEV(const SCEV *scev,
                           std::unordered_set<const Value *> &involvedVals,
                           ScalarEvolution *se);
  bool isLoopInvariantSCEV(const SCEV *scev, const Loop *L,
                           ScalarEvolution *se);

  LoopAA::ModRefResult lookForCheaperNoModRef(const Instruction *A,
                                              TemporalRelation rel,
                                              const Instruction *B,
                                              const Value *ptrB, unsigned sizeB,
                                              const Loop *L, Remedies &R,
                                              Remedies &tmpR);
};

} // namespace liberty

#endif
