#ifndef LLVM_LIBERTY_PERFORMANCE_PASS_H
#define LLVM_LIBERTY_PERFORMANCE_PASS_H

#include "llvm/Pass.h"

namespace liberty
{
  using namespace llvm;

  class PerformancePass : public ModulePass
  {
    public:
      static char ID;
      PerformancePass() : ModulePass(ID) {}

      void getAnalysisUsage(AnalysisUsage &au) const {}
      const char *getPassName() const { return "Performance Pass"; }

      bool runOnModule(Module &M);
  };
}

#endif // LLVM_LIBERTY_PERFORMANCE_PASS_H
