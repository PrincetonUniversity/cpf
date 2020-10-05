#define DEBUG_TYPE "no-capture-src-aa"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Debug.h"
#include "llvm/Analysis/ValueTracking.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "scaf/Utilities/CaptureUtil.h"
#include "scaf/Utilities/FindAllTransUses.h"

using namespace llvm;

class NoCaptureSrcAA : public ModulePass, public liberty::ClassicLoopAA {

  const DataLayout* DL;
  typedef DenseSet<const Value *> ValueSet;

public:
  static char ID;
  NoCaptureSrcAA() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
    DL = &M.getDataLayout();
    InitializeLoopAA(this, *DL);
    return false;
  }

  static bool isNoAlias(const Value *V) {
    return isa<AllocaInst>(V) || isNoAliasCall(V);
  }

  virtual AliasResult
  aliasCheck(const Pointer &P1, TemporalRelation Rel, const Pointer &P2,
             const Loop *L, Remedies &R,
             DesiredAliasResult dAliasRes = DNoOrMustAlias) {

    if (dAliasRes == DMustAlias)
      return MayAlias;

    const Value *V1 = P1.ptr;
    const Value *V2 = P2.ptr;

    if(isInterprocedural(V1,V2))
      return MayAlias;
    if(V1 == V2)
      return MayAlias;

    const Value *O1 = GetUnderlyingObject(V1, *DL);
    const Value *O2 = GetUnderlyingObject(V2, *DL);

    bool isNoAlias1 = isNoAlias(O1);
    bool isNoAlias2 = isNoAlias(O2);

    LLVM_DEBUG(errs() << "NoCapture " << *O1 << " to " << *O2 << "\n");


    if(isNoAlias1 && isNoAlias2 && O1 != O2) {
      LLVM_DEBUG(errs() << "NoCapture reporting NoAlias 1\n");
      return NoAlias;
    }

    if(isNoAlias1 && !liberty::findAllCaptures(O1)) {
      ValueSet uses;
      liberty::findAllTransUses(O1, uses);
      if(!uses.count(O2)) {
        LLVM_DEBUG(errs() << "NoCapture reporting NoAlias 2\n");
        return NoAlias;
      }
    }

    if(isNoAlias2 && !liberty::findAllCaptures(O2)) {
      ValueSet uses;
      liberty::findAllTransUses(O2, uses);
      if(!uses.count(O1)) {
        LLVM_DEBUG(errs() << "NoCapture reporting NoAlias 3\n");
        return NoAlias;
      }
    }

    return MayAlias;
  }

  StringRef getLoopAAName() const {
    return "no-capture-src-aa";
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

static RegisterPass<NoCaptureSrcAA>
X("no-capture-src-aa", "Reason about allocators that are never captured",
  false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char NoCaptureSrcAA::ID = 0;
