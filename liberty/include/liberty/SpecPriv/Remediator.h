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
#include "liberty/SpecPriv/PDG.h"
#include "liberty/SpecPriv/Critic.h"

#include <set>
#include <memory>

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

class Remedy;
typedef std::shared_ptr<Remedy> Remedy_ptr;

class Remedy {
public:
  Criticisms resolvedC;
  int cost;

  virtual void apply(PDG &pdg) = 0;
  virtual bool compare(const Remedy_ptr rhs) const = 0;
  virtual StringRef getRemedyName() const = 0;
};

struct RemedyCompare {
  bool operator()(const Remedy_ptr &lhs, const Remedy_ptr &rhs) const {
    // if same remedy type use custom comparator,
    // otherwise compare based on cost or remedy name (if cost the same)
    if (lhs->getRemedyName().equals(rhs->getRemedyName()))
      return lhs->compare(rhs);
    else if (lhs->cost == rhs->cost)
      return (lhs->getRemedyName().equals(rhs->getRemedyName()) == -1);
    else
      return lhs->cost < rhs->cost;
  }
};

typedef std::set<Remedy_ptr, RemedyCompare> Remedies;

enum DepResult { NoDep = 0, Dep = 1 };

class Remediator {
public:
  Remedies satisfy(const PDG &pdg, const Criticisms &criticisms);

  struct RemedResp {
    DepResult depRes;
    Remedy_ptr remedy;
  };

  // Query for mem deps
  virtual RemedResp memdep(const Instruction *A, const Instruction *B,
                           bool loopCarried, const Loop *L);

  // Query for RAW register dependences
  // is there a RAW reg dep from A to B?
  virtual RemedResp regdep(const Instruction *A, const Instruction *B,
                           bool loopCarried, const Loop *L);

  // Query for control dependences
  virtual RemedResp ctrldep(const Instruction *A, const Instruction *B,
                            const Loop *L);

  Remedy_ptr tryRemoveMemEdge(const Instruction *sop, const Instruction *dop,
                              bool loopCarried, const Loop *L);

  Remedy_ptr tryRemoveRegEdge(const Instruction *sop, const Instruction *dop,
                              bool loopCarried, const Loop *L);

  Remedy_ptr tryRemoveCtrlEdge(const Instruction *sop, const Instruction *dop,
                               bool loopCarried, const Loop *L);

  /// Get the name of this remediator
  virtual StringRef getRemediatorName() const = 0;
};

} // namespace liberty
#endif
