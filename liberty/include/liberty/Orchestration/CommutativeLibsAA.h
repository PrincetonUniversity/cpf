#ifndef LLVM_LIBERTY_COMM_LIBS_AA_H
#define LLVM_LIBERTY_COMM_LIBS_AA_H

#include "liberty/Analysis/LoopAA.h"
namespace liberty
{
using namespace llvm;

struct CommutativeLibsAA: public LoopAA // Not a pass!
{
  CommutativeLibsAA() : LoopAA(){}

  StringRef getLoopAAName() const { return "comm-libs-aa"; }

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Instruction *B, const Loop *L, Remedies &R);

  LoopAA::SchedulingPreference getSchedulingPreference() const
  {
    return SchedulingPreference(Low - 10);
  }

private:
  // functions that are usually considered commutative
  static const std::unordered_set<std::string> CommFunNamesSet;

  Function *getCalledFun(const Instruction *A);

};
} // namespace liberty

#endif

