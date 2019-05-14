// Modifies the code before parallelization by
// adding check for separation speculation and by reallocating
// all AUs into the appropriate heaps.
#ifndef LLVM_LIBERTY_SPECPRIV_APPLY_SEPARATION_SPECULATION_H
#define LLVM_LIBERTY_SPECPRIV_APPLY_SEPARATION_SPECULATION_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Speculation/Selector.h"

#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Recovery.h"
#include "liberty/CodeGen/RoI.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


struct ApplySeparationSpec : public ModulePass
{
  static char ID;
  ApplySeparationSpec() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  typedef std::set<const Value*> VSet;

  Module *mod;
  Type *voidty, *voidptr;
  IntegerType *u8, *u16, *u32, *u64;
  FunctionType *fv2v;
  InstInsertPt initFcn, finiFcn;
  std::vector<Loop*> loops;

  void init(ModuleLoops &mloops);

  bool runOnLoop(Loop *loop);
  bool manageMemOps(Loop *loop);
  bool manageHeaps();
  bool replaceFrees();
  bool isPrivate(Loop *loop, Value *ptr);
  bool isRedux(Loop *loop, Value *ptr);
  void insertPrivateWrite(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz);
  void insertReduxWrite(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz);
  void insertPrivateRead(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz);
  bool replacePrivateLoadsStores(Loop *loop, BasicBlock *bb);
  bool replacePrivateLoadsStores(Loop *loop);
  bool replaceReduxStores(Loop *loop, BasicBlock *bb);
  bool replaceReduxStores(Loop *loop);
  bool initFiniFcns();
  bool startInitializationFunction();
  bool startFinalizationFunction();
  bool finishInitializationFunction();
  bool finishFinalizationFunction();
  void insertMemcpy(InstInsertPt &where, Value *dst, Value *src, Value *sz);
  bool reallocateGlobals(const HeapAssignment &asgn, const HeapAssignment::AUSet &aus, const HeapAssignment::Type heap);
  bool reallocateGlobals(const HeapAssignment &asgn, const HeapAssignment::ReduxAUSet &aus);
  bool reallocateStaticAUs();
  Value *determineSize(Instruction *gravity, InstInsertPt &where, Instruction *inst);
  bool reallocateInst(const HeapAssignment &asgn, const HeapAssignment::AUSet &aus, const HeapAssignment::Type heap);
  bool reallocateInst(const HeapAssignment &asgn, const HeapAssignment::ReduxAUSet &aus);
  bool reallocateDynamicAUs();

  bool addUOChecks(Loop *loop);
  bool addUOChecks(const HeapAssignment &asgn, Loop *loop, BasicBlock *bb, VSet &alreadyInstrumented);
  void insertUOCheck(const HeapAssignment &asgn, Loop *loop, Value *obj, HeapAssignment::Type heap);
  HeapAssignment::Type selectHeap(const Value *ptr, const Loop *loop) const;
  HeapAssignment::Type selectHeap(const Value *ptr, const Ctx *ctx) const;

  const Selector &getSelector() const;
  const HeapAssignment &getHeapAssignment() const;
};

}
}


#endif

