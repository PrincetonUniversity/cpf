#ifndef LLVM_LIBERTY_COMM_LIBS_REMED_H
#define LLVM_LIBERTY_COMM_LIBS_REMED_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"

#include <unordered_set>

namespace liberty {
using namespace llvm;

class CommutativeLibsRemedy : public Remedy {
public:
  StringRef functionName;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "comm-libs-remedy"; };
};

class CommutativeLibsRemediator : public Remediator {
public:
  StringRef getRemediatorName() const { return "comm-libs-remediator"; }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   DataDepType dataDepTy, const Loop *L);

  RemedResp regdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   const Loop *L);

private:
  // functions that are usually considered commutative
  static const std::unordered_set<std::string> CommFunNamesSet;

  Function *getCalledFun(const Instruction *A);
};

} // namespace liberty

#endif
