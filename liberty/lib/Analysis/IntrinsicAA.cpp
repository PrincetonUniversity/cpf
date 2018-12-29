#define DEBUG_TYPE "intrinsic-aa"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"

#include "liberty/Analysis/ClassicLoopAA.h"

using namespace llvm;

class IntrinsicAA : public ModulePass, public liberty::ClassicLoopAA {

  static bool isRedCall(CallSite CS) {

    Function *F = CS.getCalledFunction();
    if(!F) return false;

    StringRef  name = F->getName();
    if(name.startswith("red.")) return true;

    return false;
  }

public:
  static char ID;
  IntrinsicAA() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
    const DataLayout &DL = M.getDataLayout();
    InitializeLoopAA(this, DL);
    return false;
  }

  virtual ModRefResult getModRefInfo(CallSite CS1,
                                     TemporalRelation Rel,
                                     CallSite CS2,
                                     const Loop *L) {
    if(isRedCall(CS1))
      return NoModRef;

    if(isRedCall(CS2))
      return NoModRef;

    return ModRef;
  }

  virtual ModRefResult getModRefInfo(CallSite CS,
                                     TemporalRelation Rel,
                                     const Pointer &P,
                                     const Loop *L) {
    if(isRedCall(CS))
      return NoModRef;

    return ModRef;
  }

  StringRef getLoopAAName() const {
    return "intrinsic-aa";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.setPreservesAll();                         // Does not transform code
  }

  /// getAdjustedAnalysisPointer - This method is used when a pass implements
  /// an analysis interface through multiple inheritance.  If needed, it
  /// should override this to adjust the this pointer as needed for the
  /// specified pass info.
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA*)this;
    return this;
  }
};

static RegisterPass<IntrinsicAA>
X("intrinsic-aa", "Use the properties of intrinsic", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char IntrinsicAA::ID = 0;
