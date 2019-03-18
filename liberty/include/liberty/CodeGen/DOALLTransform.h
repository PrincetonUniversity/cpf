#ifndef LLVM_LIBERTY_CODEGEN_DOALLTRANSFORM_H
#define LLVM_LIBERTY_CODEGEN_DOALLTRANSFORM_H

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/IRBuilder.h"

#include "liberty/Speculation/Selector.h"

#include "LoopDependenceInfo.hpp"
#include "Parallelization.hpp"
#include "DOALL.hpp"
#include "Techniques.hpp"

namespace liberty {
using namespace llvm;

struct DOALLTransform : public ModulePass {
  static char ID;
  DOALLTransform() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  IntegerType *int1, *int8, *int16, *int32, *int64;

  bool doallParallelizeLoop(LoopDependenceInfo *LDI, DOALL &doall);

  void linkParallelizedLoopToOriginalFunction(
      Module *module, BasicBlock *originalPreHeader,
      BasicBlock *startOfParLoopInOriginalFunc,
      BasicBlock *endOfParLoopInOriginalFunc, Value *envArray,
      Value *envIndexForExitVariable,
      SmallVector<BasicBlock *, 10> &loopExitBlocks);
};

} // namespace liberty

#endif
