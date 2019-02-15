#ifndef LLVM_LIBERTY_HEADER_PHI_PROF_H
#define LLVM_LIBERTY_HEADER_PHI_PROF_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "llvm/Analysis/LoopInfo.h"

namespace liberty
{

using namespace llvm;

class HeaderPhiProfiler: public ModulePass
{
public:
  static char ID;
  HeaderPhiProfiler();
  ~HeaderPhiProfiler();

  void getAnalysisUsage(AnalysisUsage& au) const;

  bool runOnModule(Module& m);

private:
  void instrumentLoop(Loop* loop, Function* invoke, Function* iter);
};

}

#endif
