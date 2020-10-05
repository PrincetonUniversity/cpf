#ifndef LLVM_LIBERTY_REMEDIATOR_H
#define LLVM_LIBERTY_REMEDIATOR_H

#include "llvm/Pass.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Critic.h"
#include "liberty/Speculation/Api.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Strategy/PerformanceEstimator.h"
#include "scaf/Utilities/InsertPrintf.h"
#include "scaf/Utilities/InstInsertPt.h"

#include "Assumptions.h"
#include "PDG.hpp"
#include "LoopDependenceInfo.hpp"
#include "Task.hpp"

#include <set>
#include <unordered_set>
#include <memory>

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

typedef std::unique_ptr<Criticisms> Criticisms_uptr;

typedef std::unordered_set<const Instruction *> InstSet;
typedef std::unique_ptr<InstSet> InstSet_uptr;

enum DepResult { NoDep = 0, Dep = 1 };

enum DataDepType { RAW = 0, WAW = 1, WAR = 2 };

class Remediator {
public:
  virtual Remedies satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms);

  struct RemedResp {
    DepResult depRes;
    Remedy_ptr remedy;
  };

  struct RemedCriticResp {
    DepResult depRes;
    Remedy_ptr remedy;
    Criticisms_uptr criticisms;
  };

  // Query for mem deps
  virtual RemedResp memdep(const Instruction *A, const Instruction *B,
                           bool loopCarried, DataDepType dataDepTy,
                           const Loop *L);

  // Query for RAW register dependences
  // is there a RAW reg dep from A to B?
  virtual RemedResp regdep(const Instruction *A, const Instruction *B,
                           bool loopCarried, const Loop *L);

  // Query for control dependences
  virtual RemedResp ctrldep(const Instruction *A, const Instruction *B,
                            const Loop *L);

  Remedy_ptr tryRemoveMemEdge(const Instruction *sop, const Instruction *dop,
                              bool loopCarried, DataDepType dataDepTy,
                              const Loop *L);

  Remedy_ptr tryRemoveRegEdge(const Instruction *sop, const Instruction *dop,
                              bool loopCarried, const Loop *L);

  Remedy_ptr tryRemoveCtrlEdge(const Instruction *sop, const Instruction *dop,
                               bool loopCarried, const Loop *L);

  /// Get the name of this remediator
  virtual StringRef getRemediatorName() const = 0;

  static double estimate_validation_weight(PerformanceEstimator *perf,
                                           const Instruction *gravity,
                                           unsigned long validation_weight);

  static bool noMemoryDep(const Instruction *src, const Instruction *dst,
                          LoopAA::TemporalRelation FW,
                          LoopAA::TemporalRelation RV, const Loop *loop,
                          LoopAA *aa, bool rawDep, bool wawDep, Remedies &R);

  virtual LoopAA::ModRefResult modref_many(const Instruction *A,
                                           LoopAA::TemporalRelation rel,
                                           const Instruction *B, const Loop *L,
                                           Remedies &R);

  virtual LoopAA::ModRefResult
  modref_with_ptrs(const Instruction *A, const Value *ptrA,
                   LoopAA::TemporalRelation rel, const Instruction *B,
                   const Value *ptrB, const Loop *L, Remedies &R) {
    return LoopAA::ModRef;
  }
};

} // namespace liberty
#endif
