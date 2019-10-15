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
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Speculation/Api.h"
#include "Assumptions.h"
#include "PDG.hpp"
#include "LoopDependenceInfo.hpp"
#include "TaskExecution.hpp"


#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Classify.h"

#include <set>
#include <unordered_set>
#include <memory>

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

/*
class Remedy;
typedef std::shared_ptr<Remedy> Remedy_ptr;

class Remedy {
public:
  Criticisms resolvedC;
  int cost;

  Task *task;
  Loop *loop;
  const DataLayout *DL;

  const Read *read;
  const HeapAssignment *asgn;
  std::set<const Value*> *alreadyInstrumented;
  Module *mod;
  Type *voidty, *voidptr;
  IntegerType *u8, *u32;

  virtual void apply(Task *task) = 0;
  virtual bool compare(const Remedy_ptr rhs) const = 0;
  virtual StringRef getRemedyName() const = 0;

  HeapAssignment::Type selectHeap(const Value *ptr, const Loop *loop) const;
  HeapAssignment::Type selectHeap(const Value *ptr, const Ctx *ctx) const;
  bool isPrivate(Value *ptr);
  void insertPrivateWrite(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz);
  void insertReduxWrite(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz);
  void insertPrivateRead(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz);
  bool replacePrivateLoadsStore(Instruction *origI);
  bool replaceReduxStore(StoreInst *origSt);
};

struct RemedyCompare {
  bool operator()(const Remedy_ptr &lhs, const Remedy_ptr &rhs) const {
    // if same remedy type use custom comparator,
    // otherwise compare based on cost or remedy name (if cost the same)
    if (lhs->getRemedyName().equals(rhs->getRemedyName()))
      return lhs->compare(rhs);
    else if (lhs->cost == rhs->cost)
      return (lhs->getRemedyName().compare(rhs->getRemedyName()) == -1);
    else
      return lhs->cost < rhs->cost;
  }
};

typedef std::set<Remedy_ptr, RemedyCompare> Remedies;

typedef std::shared_ptr<Remedies> Remedies_ptr;

struct RemediesCompare {
  bool operator()(const Remedies_ptr &lhs, const Remedies_ptr &rhs) const {

    RemedyCompare remedyCompare;

    // compute total costs
    unsigned costLHS = 0;
    for (auto r : *lhs) {
      costLHS += r->cost;
    }
    unsigned costRHS = 0;
    for (auto r : *rhs) {
      costRHS += r->cost;
    }

    // if different sizes compare their costs
    if (lhs->size() != rhs->size()) {
      return costLHS < costRHS;
    }

    auto itLHS = lhs->begin();
    auto itRHS = rhs->begin();
    bool identical = true;
    while (itLHS != lhs->end()) {
      // check if each remedy is equal
      if (remedyCompare(*itLHS, *itRHS) || remedyCompare(*itRHS, *itLHS)) {
        identical = false;
        break;
      }
      ++itLHS;
      ++itRHS;
    }
    if (identical)
      return false;

    return costLHS < costRHS;
  }
};

typedef std::set<Remedies_ptr, RemediesCompare> SetOfRemedies;

*/

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

  bool noMemoryDep(const Instruction *src, const Instruction *dst,
                   LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV,
                   const Loop *loop, LoopAA *aa, bool rawDep, Remedies &R);

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
