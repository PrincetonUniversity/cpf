#ifndef LLVM_LIBERTY_SPECPRIV_HEAP_PROFILE_H
#define LLVM_LIBERTY_SPECPRIV_HEAP_PROFILE_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"

#include <map>
#include <vector>

namespace liberty
{
namespace SpecPriv
{

using namespace std;
using namespace llvm;

class HeapProfile: public ModulePass
{
public:
  static char ID;
  HeapProfile() : ModulePass(ID) { }
  ~HeapProfile() { }

  void getAnalysisUsage(AnalysisUsage& au) const;

  bool runOnModule(Module& m);

private:
  bool findTarget(Module& m);

  void collectStageInfo(Module& m, Loop* loop);

  void replaceExternalFunctionCalls(Module& m);

  void instrumentConstructor(Module& m);
  void instrumentDestructor(Module& m);

  void instrumentLoopStartStop(Module&m, Loop* l);
  void instrumentInstructions(Module& m, Loop* l);

  int  getIndex(PointerType* ty, size_t& size);
  void instrumentMemIntrinsics(Module& m, MemIntrinsic* mi);
  void instrumentLoopInst(Module& m, Instruction* inst, uint32_t id);
  void instrumentExtInst(Module& m, Instruction* inst, uint32_t id);

  void addWrapperImplementations(Module& m);

  // frequently used types
  
  Type *Void, *I32, *I64, *I8Ptr, *U8, *VoidPtr;

  Function* target_fn;
  Loop*     target_loop;

  typedef std::map<uint32_t, uint8_t> Inst2StageSign;

  Inst2StageSign inst2stagesign;
};

} // namespace SpecPriv
} // namespace liberty

#endif
