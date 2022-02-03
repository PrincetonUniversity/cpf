#ifndef LLVM_LIBERTY_SLAMP_SLAMP_H
#define LLVM_LIBERTY_SLAMP_SLAMP_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Utilities/StaticID.h"

#include <set>

namespace liberty::slamp
{

using namespace std;
using namespace llvm;

class SLAMP: public ModulePass
{
public:
  static char ID;
  SLAMP();
  ~SLAMP();

  void getAnalysisUsage(AnalysisUsage& au) const;

  bool runOnModule(Module& m);

private:
  bool findTarget(Module& m);

  bool mayCallSetjmpLongjmp(Loop* loop);
  void getCallableFunctions(Loop* loop, set<Function*>& callables);
  void getCallableFunctions(Function* f, set<Function*>& callables);
  void getCallableFunctions(CallInst* ci, set<Function*>& callables);
  void getFunctionsWithSign(CallInst* ci, set<Function*> matched);

  void replaceExternalFunctionCalls(Module& m);

  Function* instrumentConstructor(Module& m);
  void instrumentDestructor(Module& m);
  void instrumentGlobalVars(Module& m, Function* ctor);

  void instrumentNonStandards(Module& m, Function* ctor);
  void allocErrnoLocation(Module& m, Function* ctor);

  void instrumentLoopStartStop(Module&m, Loop* l);
  void instrumentInstructions(Module& m, Loop* l);

  void instrumentMainFunction(Module& m);

  int  getIndex(PointerType* ty, size_t& size, const DataLayout& DL);
  void instrumentMemIntrinsics(Module& m, MemIntrinsic* mi);
  void instrumentLifetimeIntrinsics(Module& m, Instruction* inst);
  void instrumentLoopInst(Module& m, Instruction* inst, uint32_t id);
  void instrumentExtInst(Module& m, Instruction* inst, uint32_t id);

  void addWrapperImplementations(Module& m);

  // frequently used types

  Type *Void, *I32, *I64, *I8Ptr;

  StaticID* sid;

  Function* target_fn;
  Loop*     target_loop;
};

} // namespace liberty::slamp

#endif
