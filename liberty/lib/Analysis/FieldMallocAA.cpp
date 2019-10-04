#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InstIterator.h"

#include "liberty/Utilities/CaptureUtil.h"
#include "liberty/Utilities/FindAllTransUses.h"

#include "liberty/Analysis/TypeSanity.h"

using namespace llvm;

class FieldMallocAA : public ModulePass, public liberty::ClassicLoopAA {

  typedef DenseSet<const Value *> ValueSet;
  typedef ValueSet::iterator ValueSetIt;

  liberty::TypeSanityAnalysis *TAA;

  DenseSet<Type *> badFieldTypes;

  const DataLayout *DL;

  static bool isAllocSrc(const Value *v) {

    if(isa<AllocaInst>(v)) return false;

    const CallInst *call = dyn_cast<CallInst>(v);
    if(!call) return false;

    Function *fun = call->getCalledFunction();
    if(!fun) return false;

    return fun->returnDoesNotAlias();
  }

  static bool isUniqueAllocSrc(const StoreInst *store) {

    const Module *M = store->getParent()->getParent()->getParent();
    const DataLayout &td = M->getDataLayout();
    const Value *O = GetUnderlyingObject(store->getValueOperand(), td);
    if(!isAllocSrc(O)) return false;

    liberty::CaptureSet captureSet;
    liberty::findAllCaptures(O, &captureSet);

    return captureSet.size() == 1;
  }

  static bool isSafelyUsed(ValueSet &uses) {
    const ValueSetIt B = uses.begin();
    const ValueSetIt E = uses.end();
    for(ValueSetIt use = B; use != E; ++use) {
      const StoreInst *store = dyn_cast<StoreInst>(*use);
      if(store && !isUniqueAllocSrc(store))
        return false;
    }

    return true;
  }

public:
  static char ID;
  FieldMallocAA() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
    DL = &M.getDataLayout();
    InitializeLoopAA(this, *DL);

    TAA = &getAnalysis<liberty::TypeSanityAnalysis>();

    typedef Module::iterator ModIt;
    const ModIt B = M.begin();
    const ModIt E = M.end();
    for(ModIt fun = B; fun != E; ++fun) {
      if(!fun->isDeclaration())
        runOnFunction(*fun);
    }

    return false;
  }

  bool runOnFunction(Function &F) {

    const inst_iterator B = inst_begin(F);
    const inst_iterator E = inst_end(F);
    for(inst_iterator inst = B; inst != E; ++inst) {
      runOnGEP(&*inst);
    }

    return false;
  }

  void runOnGEP(Instruction *inst) {
    GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(inst);
    if(!gep) return;

    if(!gep->getPointerOperandType()->isPointerTy()) return;

    PointerType *ptrOpTy = cast<PointerType>(gep->getPointerOperandType());
    if(!ptrOpTy->getElementType()->isPointerTy()) return;

    bool isSane = TAA->isSane(ptrOpTy);
    if(!isSane) return;

    ValueSet uses;
    liberty::findAllTransUses(gep, uses);

    if(isSafelyUsed(uses)) return;

    const gep_type_iterator B = gep_type_begin(gep);
    const gep_type_iterator E = gep_type_end(gep);
    //sot : operator* is no longer supported in LLVM 5.0 for gep_type_iterator
    for(gep_type_iterator type = B; type != E; ++type)
    {
      if (StructType *STy = type.getStructTypeOrNull())
        badFieldTypes.insert(STy);
      else
        badFieldTypes.insert(type.getIndexedType());
    }
  }

  AliasResult aliasCheck(const Pointer &P1, TemporalRelation rel,
                         const Pointer &P2, const Loop *L,
                         liberty::Remedies &R) {

    const Value *O1 = GetUnderlyingObject(P1.ptr, *DL);
    const Value *O2 = GetUnderlyingObject(P2.ptr, *DL);

    const LoadInst *L1 = dyn_cast<LoadInst>(O1);
    const LoadInst *L2 = dyn_cast<LoadInst>(O2);

    if(!L1 || !L2) return MayAlias;

    const GetElementPtrInst *GEP1 =
      dyn_cast<GetElementPtrInst>(L1->getPointerOperand());

    const GetElementPtrInst *GEP2 =
      dyn_cast<GetElementPtrInst>(L2->getPointerOperand());

    if(!GEP1 || !GEP2) return MayAlias;

    PointerType *Ty1 = dyn_cast<PointerType>(GEP1->getPointerOperandType());
    PointerType *Ty2 = dyn_cast<PointerType>(GEP2->getPointerOperandType());

    if(!Ty1 || !isa<StructType>(Ty1->getElementType())) return MayAlias;
    if(!Ty2 || !isa<StructType>(Ty2->getElementType())) return MayAlias;

    if(badFieldTypes.count(Ty1) || badFieldTypes.count(Ty2)) return MayAlias;

    return getTopAA()->alias(GEP1->getPointerOperand(), 1, rel,
                             GEP2->getPointerOperand(), 1, L, R);
  }

  StringRef getLoopAAName() const {
    return "field-malloc-aa";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired<liberty::TypeSanityAnalysis>();
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

char FieldMallocAA::ID = 0;

static RegisterPass<FieldMallocAA>
X("field-malloc-aa", "Alias analysis for field pointers", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

