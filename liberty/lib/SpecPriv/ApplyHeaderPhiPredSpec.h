#ifndef LLVM_LIBERTY_SPECPRIV_APPLY_HEADER_PHI_PREDICTION_SPECULATION_H
#define LLVM_LIBERTY_SPECPRIV_APPLY_HEADER_PHI_PREDICTION_SPECULATION_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/ModuleLoops.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct ApplyHeaderPhiPredSpec : public ModulePass
{
  static char ID;
  ApplyHeaderPhiPredSpec() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  Module* m;
  Type *voidty, *voidptr;
  IntegerType *u8, *u16, *u32, *u64;
  std::vector<Loop*> loops;

  void init(ModuleLoops& mloops);

  bool runOnLoop(Loop* loop);
};

}
}

#endif
