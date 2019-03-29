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
#include "liberty/Speculation/Recovery.h"

#include "LoopDependenceInfo.hpp"
#include "Parallelization.hpp"
#include "DOALL.hpp"
#include "DOALLTask.hpp"
#include "Techniques.hpp"

#include <unordered_set>
#include <memory>

namespace liberty {
using namespace llvm;

struct DOALLTransform : public ModulePass {
  static char ID;
  DOALLTransform() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  std::unique_ptr<DOALL> doall;

  Selector *selector;
  bool customHeapAlloc;
  bool nonSpecPrivRedux;

  IntegerType *int8;
  IntegerType *int16;
  IntegerType *int32;
  IntegerType *int64;
  Type *voidty, *voidptr;
  Module *mod;

  FunctionType *fv2v;
  InstInsertPt initFcn, finiFcn;

  const Read *read;
  HeapAssignment *asgn;
  DOALLTask *task;

  std::unordered_set<AU*> nonSpecReduxAUs;
  std::unordered_set<AU*> nonSpecPrivAUs;

  void adjustForSpecDOALL(LoopDependenceInfo *LDI);
  void specDOALLInvocation(LoopDependenceInfo *LDI);
  void markIterationBoundaries();
  bool doallParallelizeLoop(LoopDependenceInfo *LDI,
                            SelectedRemedies *selectedRemeds);

  void reallocateEnvAsShared(Value *alloc, Ctx *fcn_ctx);
  bool demoteLiveOutsAndPhis(Loop *loop, LiveoutStructure &liveoutStructure);
  bool manageHeaps();
  bool manageNonSpecHeaps();
  bool replaceFrees();
  bool reallocateDynamicAUs();
  bool reallocateInst(const HeapAssignment::ReduxAUSet &aus);
  bool reallocateInst(const HeapAssignment::AUSet &aus,
                      const HeapAssignment::Type heap);
  bool reallocateStaticAUs();
  Value *determineSize(Instruction *gravity, InstInsertPt &where, Instruction *inst);
  HeapAssignment::Type selectHeap(const Value *ptr, const Ctx *ctx) const;
  bool reallocateGlobals(const HeapAssignment::ReduxAUSet &aus);
  bool reallocateGlobals(const HeapAssignment::AUSet &aus,
                         const HeapAssignment::Type heap);
  void insertMemcpy(InstInsertPt &where, Value *dst, Value *src, Value *sz);
  bool finishFinalizationFunction();
  bool finishInitializationFunction();
  bool startFinalizationFunction();
  bool startInitializationFunction();
};

} // namespace liberty

#endif
