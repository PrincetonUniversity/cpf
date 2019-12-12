#define DEBUG_TYPE "global-malloc-aa"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/FindSource.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Utilities/CaptureUtil.h"


using namespace llvm;

class GlobalMallocAA : public ModulePass, public liberty::ClassicLoopAA {

private:
  typedef Module::const_global_iterator GlobalIt;
  typedef Value::const_user_iterator UseIt;

  typedef DenseSet<const Value *> ValueSet;
  typedef SmallPtrSet<const Instruction *, 1> CISet;
  typedef CISet::const_iterator CISetIt;
  typedef DenseMap<const Value *, CISet> VToCIMap;

  ValueSet nonMalloc;
  ValueSet nonExclusive;
  VToCIMap mallocSrcs;
  VToCIMap nonMallocSrcs;

  const DataLayout *DL;
  const TargetLibraryInfo *tli;

  static bool isExclusive(const GlobalValue *global,
                          const ValueSet &nonMalloc,
                          const CISet &sources) {

    if(nonMalloc.count(global))
      return false;

    if(liberty::findAllCaptures(global))
      return false;

    liberty::CaptureSet captureSet;

    for(CISetIt src = sources.begin(); src != sources.end(); ++src) {

      liberty::findAllCaptures(*src, &captureSet);
      assert(captureSet.size() != 0 && "How can a source not be captured!?");
      if(captureSet.size() > 1) {
        return false;
      }

      captureSet.clear();
    }

    for(UseIt use = global->user_begin(); use != global->user_end(); ++use) {
      if(isa<LoadInst>(*use) && liberty::findAllCaptures(*use)) {
        return false;
      }
    }

    LLVM_LLVM_DEBUG(errs() << "Exclusive global: " << *global << "\n");

    return true;
  }

  bool storeOtherGV(const StoreInst *sI, const GlobalValue *otherGV) const {
    const LoadInst *ld = dyn_cast<LoadInst>(sI->getValueOperand());
    if (!ld)
      return false;

    if (ld->getPointerOperand() == otherGV)
      return true;

    if (const BitCastOperator *bcOp =
            dyn_cast<BitCastOperator>(ld->getPointerOperand())) {
      if (const GlobalValue *gv = dyn_cast<GlobalValue>(bcOp->getOperand(0))) {
        if (gv == otherGV)
          return true;
      }
    }
    return false;
  }

  bool storeNull(const StoreInst *sI) const {
    const Value *stValOp = sI->getValueOperand();
    if (PointerType *stValOpPtrTy = dyn_cast<PointerType>(stValOp->getType())) {
      auto nullPtrVal =
          ConstantPointerNull::get(cast<PointerType>(stValOpPtrTy));
      if (sI->getValueOperand() == nullPtrVal)
        return true;
    }
    return false;
  }

  bool nonCaptureFunCall(const User *use, const GlobalVariable *global) const {
    bool nocaptureF = false;
    if (const CallInst *call = dyn_cast<CallInst>(use)) {
      const Function *fcn = call->getCalledFunction();
      if (!fcn)
        return false;
      for (unsigned i = 0; i < call->getNumArgOperands(); ++i) {
        const Value *arg = call->getArgOperand(i);
        if (const GlobalVariable *gvC = dyn_cast<GlobalVariable>(arg)) {
          if (gvC != global)
            continue;

          if (fcn->hasParamAttribute(i, Attribute::NoCapture))
            nocaptureF = true;
          else
            return false;
        }
      }
    }
    else
      return false;

    return nocaptureF;
  }

public:
  static char ID;
  GlobalMallocAA() : ModulePass(ID) {}

  virtual bool runOnModule(Module &M) {
    DL = &M.getDataLayout();
    InitializeLoopAA(this, *DL);

    tli = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

    nonMalloc.clear();
    nonMallocSrcs.clear();
    mallocSrcs.clear();

    for (GlobalIt global = M.global_begin(); global != M.global_end();
         ++global) {
      Type *type = global->getType()->getElementType();
      if (type->isPointerTy()) {
        for (UseIt use = global->user_begin(); use != global->user_end();
             ++use) {
          if (const StoreInst *store = dyn_cast<StoreInst>(*use)) {
            const Instruction *src = liberty::findNoAliasSource(store, *tli);
            if (src) {
              mallocSrcs[&*global].insert(src);
            } else {
              if (storeNull(store))
                continue;
              nonMalloc.insert(&*global);
              nonMallocSrcs[&*global].insert(store);
            }
          } else if (const BitCastOperator *bcOp =
                         dyn_cast<BitCastOperator>(*use)) {
            for (UseIt bUse = bcOp->user_begin(); bUse != bcOp->user_end();
                 ++bUse) {
              if (const StoreInst *bStore = dyn_cast<StoreInst>(*bUse)) {
                const Instruction *s = liberty::findNoAliasSource(bStore, *tli);
                if (s) {
                  mallocSrcs[&*global].insert(s);
                } else {
                  // null is stored usually after a free operation.
                  // Storing null should not be considered nonMalloc.
                  // Undef behavior if the pointer is used with null value.
                  // So we can leverage that and increase GlobalMallocAA
                  // applicability
                  if (storeNull(bStore))
                    continue;
                  nonMalloc.insert(&*global);
                  nonMallocSrcs[&*global].insert(bStore);
                }
              } else if (!isa<LoadInst>(*bUse) && !nonCaptureFunCall(*use, &*global)) {
                nonMalloc.insert(&*global);
              }
            }
          } else if (!isa<LoadInst>(*use) && !nonCaptureFunCall(*use, &*global)) {
            nonMalloc.insert(&*global);
          }
        }
      }

      if(!global->hasLocalLinkage() && !liberty::FULL_UNIVERSAL) {
        nonMalloc.insert(&*global);
      }
    }

    for(GlobalIt global = M.global_begin(); global != M.global_end(); ++global) {
      if(!isExclusive(&*global, nonMalloc, mallocSrcs[&*global])) {
        nonExclusive.insert(&*global);
      }
    }

    return false;
  }

  virtual AliasResult
  aliasCheck(const Pointer &P1, TemporalRelation Rel, const Pointer &P2,
             const Loop *L, Remedies &R,
             DesiredAliasResult dAliasRes = DNoOrMustAlias) {

    if (dAliasRes == DMustAlias)
      return MayAlias;

    const Value *V1 = P1.ptr, *V2 = P2.ptr;

    const GlobalValue *V1GlobalSrc = liberty::findGlobalSource(V1);
    const GlobalValue *V2GlobalSrc = liberty::findGlobalSource(V2);

    const bool V1Exclusive = !nonExclusive.count(V1GlobalSrc);
    const bool V2Exclusive = !nonExclusive.count(V2GlobalSrc);

    if(V1GlobalSrc && V1Exclusive &&
       V1GlobalSrc != V2GlobalSrc &&
       !liberty::findLoadedNoCaptureArgument(V2, *DL))
      return NoAlias;

    if(V2GlobalSrc && V2Exclusive &&
       V2GlobalSrc != V1GlobalSrc &&
       !liberty::findLoadedNoCaptureArgument(V1, *DL))
      return NoAlias;

    if(V1GlobalSrc && V1Exclusive &&
       V2GlobalSrc && V2Exclusive &&
       V1GlobalSrc == V2GlobalSrc)
      return MayAlias;


    const bool V1MallocOnly = !nonMalloc.count(V1GlobalSrc);
    const bool V2MallocOnly = !nonMalloc.count(V2GlobalSrc);

    if(V1GlobalSrc && V1MallocOnly &&
       V2GlobalSrc && V2MallocOnly) {

      const CISet V1set = mallocSrcs[V1GlobalSrc];
      const CISet V2set = mallocSrcs[V2GlobalSrc];

      SmallPtrSet<const Instruction *, 4> both;
      for(CISetIt it = V1set.begin(); it != V1set.end(); ++it) {
        both.insert(*it);
      }

      bool noAlias = true;
      for(CISetIt it = V2set.begin(); it != V2set.end(); ++it) {
        if(!both.insert(*it).second) {
          noAlias = false;
        }
      }

      if(both.size() && noAlias) {
        return LoopAA::NoAlias;
      }
    }

    const Instruction *V1Src = liberty::findNoAliasSource(V1, *tli);
    const Instruction *V2Src = liberty::findNoAliasSource(V2, *tli);

    if(V1GlobalSrc && V2Src && V1MallocOnly &&
       !mallocSrcs[V1GlobalSrc].count(V2Src)) {
      return NoAlias;
    }

    if(V2GlobalSrc && V1Src && V2MallocOnly &&
       !mallocSrcs[V2GlobalSrc].count(V1Src)) {
      return NoAlias;
    }

    const GlobalValue *V1Global =
      dyn_cast<GlobalValue>(GetUnderlyingObject(V1, *DL));
    const GlobalValue *V2Global =
      dyn_cast<GlobalValue>(GetUnderlyingObject(V2, *DL));

    if(V1GlobalSrc && V2Global && V1MallocOnly)
      return NoAlias;

    if(V2GlobalSrc && V1Global && V2MallocOnly)
      return NoAlias;

    // Addressing the following scenario:
    // V1 points to one object and V2 points to a different object and at some
    // point these two global pointers get swapped. The stores in the swap are
    // the only nonMalloc stores.  If the swap does not occur within the loop of
    // interest then these two pointers do not alias. They will never point to
    // the same object at any invocation of this loop.
    if (V1GlobalSrc && !V1MallocOnly && V2GlobalSrc && !V2MallocOnly && L) {

      // 1) check that the global pointers originally point to different objects
      const CISet V1set = mallocSrcs[V1GlobalSrc];
      const CISet V2set = mallocSrcs[V2GlobalSrc];

      SmallPtrSet<const Instruction *, 4> both;
      for (CISetIt it = V1set.begin(); it != V1set.end(); ++it) {
        both.insert(*it);
      }

      bool noAlias = true;
      for (CISetIt it = V2set.begin(); it != V2set.end(); ++it) {
        if (!both.insert(*it).second) {
          noAlias = false;
        }
      }

      if (both.size() && noAlias) {

        // 2) check whether existence of nonMallocSrcs is due to a swap of the
        // two global pointers
        if (nonMallocSrcs[V1GlobalSrc].size() == 1 &&
            nonMallocSrcs[V2GlobalSrc].size() == 1) {
          const StoreInst *nonMallocSt1 =
              dyn_cast<StoreInst>(*nonMallocSrcs[V1GlobalSrc].begin());
          const StoreInst *nonMallocSt2 =
              dyn_cast<StoreInst>(*nonMallocSrcs[V2GlobalSrc].begin());

          if (nonMallocSt1 && storeOtherGV(nonMallocSt1, V2GlobalSrc) &&
              nonMallocSt2 && storeOtherGV(nonMallocSt2, V1GlobalSrc) &&
              // stores should occur within the same basic block
              nonMallocSt1->getParent() == nonMallocSt2->getParent()) {

            // nonMallocSrcs exist due to a swap
            //
            // 3) check that swap does not occur within the loop of interest
            if (!L->contains(nonMallocSt1) && !L->contains(nonMallocSt2))
              return NoAlias;
          }
        }
      }
    }

    return MayAlias;
  }

  StringRef getLoopAAName() const {
    return "global-malloc-aa";
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

char GlobalMallocAA::ID = 0;

static RegisterPass<GlobalMallocAA>
X("global-malloc-aa", "Alias analysis for globals pointers", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

