#define DEBUG_TYPE "disjoint-field-aa"

#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/FindSource.h"
#include "liberty/Analysis/TypeSanity.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/CaptureUtil.h"

#include "NoEscapeFieldsAA.h"

using namespace llvm;

STATISTIC(numNoAliases, "Number of no-alias results given by disjoint fields");

class DisjointFieldsAA : public ModulePass, public liberty::ClassicLoopAA {

  const liberty::TypeSanityAnalysis *TAA;
  const liberty::NonCapturedFieldsAnalysis *NEFAA;

  DenseSet<StructType *> jointTypes;

  const DataLayout *DL;
  const TargetLibraryInfo *tli;

public:
  static char ID;
  DisjointFieldsAA() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
    DL = &M.getDataLayout();
    InitializeLoopAA(this, *DL);

    tli = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

    TAA = &getAnalysis<liberty::TypeSanityAnalysis>();

    NEFAA = &getAnalysis<liberty::NonCapturedFieldsAnalysis>();

    typedef Module::iterator ModuleIt;
    for(ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
      if(!fun->isDeclaration())
        runOnFunction(*fun);
    }

    return false;
  }

  void runOnFunction(const Function &F) {

    for(const_inst_iterator inst = inst_begin(F); inst != inst_end(F); ++inst) {
      if(const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(&*inst))
        runOnInstruction(gep);
    }
  }

  typedef DenseSet<const Instruction *> InstSet;
  typedef DenseSet<const StoreInst *> StoreSet;

  static bool isSafeCall(const CallSite &CS) {

    if(isa<MemIntrinsic>(CS.getInstruction()))
      return true;

    const Function *F = CS.getCalledFunction();
    if(!F) return false;

    if(F->getName() == "free")
      return true;

    return false;
  }

  static bool findAllPtrDefs(const Instruction *I, const Value *V,
                          StoreSet &stores, InstSet &visited) {

    const Instruction *inst = dyn_cast<Instruction>(V);
    if(!inst) return true;

    if(isa<LoadInst>(inst))
      return true;

    CallSite CS = liberty::getCallSite(inst);
    if(CS.getInstruction())
      return isSafeCall(CS);

    const StoreInst *store = dyn_cast<StoreInst>(inst);
    if(!store) {

      if(!isa<PointerType>(inst->getType()))
        return true;

      return findAllPtrDefs(inst, stores, visited);
    }

    // This is only legal because the involved types are sane.
    if(store->getPointerOperand() == I &&
       store->getValueOperand()->getType()->isPointerTy())
       stores.insert(store);

    return true;
  }

  static bool findAllPtrDefs(const Instruction *I, StoreSet &stores,
                          InstSet &visited) {

    if(!visited.insert(I).second)
      return true;

    typedef Value::const_user_iterator UseIt;
    for(UseIt use = I->user_begin(); use != I->user_end(); ++use) {
      if(!findAllPtrDefs(I, *use, stores, visited))
        return false;
    }

    return true;
  }

  static bool findAllPtrDefs(const Instruction *I, StoreSet &stores) {
    InstSet visited;
    return findAllPtrDefs(I, stores, visited);
  }

  typedef std::vector<StructType *> Structs;

  static Structs getAffectedStructs(const GetElementPtrInst *gep) {

    Structs structs;

    Type *baseType = gep->getPointerOperand()->getType();

    std::vector<Value *> ops;
    typedef GetElementPtrInst::const_op_iterator OpIt;
    for(OpIt op = gep->idx_begin(); op != gep->idx_end(); ++op) {
      ops.push_back(*op);
    }

    for(unsigned i = 1; i < ops.size(); ++i) {
      Type *type =
        GetElementPtrInst::getIndexedType(baseType, ArrayRef<Value *>(ops));

      //sot
      if (!type) continue;  // to prevent a seg fault when a null pointer is passed to dyn_cast

      if(StructType *structTy = dyn_cast<StructType>(type))
        structs.push_back(structTy);
    }

    return structs;
  }

  void runOnInstruction(const GetElementPtrInst *gep ) {

    // If the result of the GEP is a pointer
    Type *gepType = gep->getType();
    if(!isa<PointerType>(gepType)) return;

    // If the GEP's pointer operand is a sane type
    if(!TAA->isSane(gep->getPointerOperand()->getType())) return;

    // If the number of affected structs is greater than one, give up and mark
    // them all as joint.
    Structs structs = getAffectedStructs(gep);
    if(structs.size() == 0) return;
    if(structs.size() > 1) {

      for(unsigned i = 0; i < structs.size(); ++i)
        jointTypes.insert(structs[i]);

      return;
    }

    StructType *structType = structs[0];

    if(isJoint(gep, structType, *tli))
      jointTypes.insert(structType);

  }

  static bool isJoint(const GetElementPtrInst *gep, StructType *structType,
                      const TargetLibraryInfo &tli) {

    // If we cannot find all the defs of the pointer, mark the structure joint.
    StoreSet defs;
    if(!findAllPtrDefs(gep, defs))
      return true;

    // For each definition of the pointer
    typedef StoreSet::iterator DefIt;
    for(DefIt def = defs.begin(); def != defs.end(); ++def) {

      // Make sure the stored pointer is from a NoAlias source.
      const Value *src =
        liberty::findNoAliasSource((*def)->getValueOperand(), tli);
      if(!src) return true;

      // If all the captures for the source are not known, mark the type joint.
      liberty::CaptureSet captures;
      if(!liberty::findAllCaptures(src, &captures)) {
        DEBUG(errs() << structType->getName() << " incomplete capture!\n");
        return true;
      }

      // If there is more than one capture, prove the affected types of the
      // capture are different.
      if(captures.size() != 1) {

        DenseSet<Type *> capturingTypes;

        typedef liberty::CaptureSet::iterator CapSetIt;
        for(CapSetIt cap = captures.begin(); cap != captures.end(); ++cap) {

          const StoreInst *store = dyn_cast<StoreInst>(*cap);
          if(!store) return true;

          const Value *ptr = store->getPointerOperand();
          const GetElementPtrInst *capSrc = dyn_cast<GetElementPtrInst>(ptr);
          if(!capSrc) return true;

          Structs structs = getAffectedStructs(capSrc);
          for(unsigned i = 0; i < structs.size(); ++i) {
            if(!capturingTypes.insert(structs[i]).second)
              return true;
          }
        }
      }
    }

    return false;
  }

  static bool fieldsDiffer(const GetElementPtrInst *GEP1,
                           const GetElementPtrInst *GEP2) {

    if(!GEP1->hasAllConstantIndices())
      return false;

    if(!GEP2->hasAllConstantIndices())
      return false;

    if(GEP1->getNumIndices() != GEP2->getNumIndices())
      return false;

    typedef GetElementPtrInst::const_op_iterator OpIt;
    OpIt op1 = GEP1->idx_begin();
    OpIt op2 = GEP2->idx_begin();

    ++op1;
    ++op2;

    while(op1 != GEP1->idx_end()) {

      if(op1 != op2)
        return true;

      ++op1;
      ++op2;
    }

    return false;
  }

  virtual AliasResult aliasCheck(const Pointer &P1,
                                 TemporalRelation Rel,
                                 const Pointer &P2,
                                 const Loop *L) {

    const Value *V1 = P1.ptr;
    const Value *V2 = P2.ptr;

    if(V1 == V2) return MayAlias;

    const Value *O1 = GetUnderlyingObject(V1, *DL);
    const Value *O2 = GetUnderlyingObject(V2, *DL);

    const LoadInst *L1 = dyn_cast<LoadInst>(O1);
    const LoadInst *L2 = dyn_cast<LoadInst>(O2);

    if(!L1 || !L2) return MayAlias;

    const GetElementPtrInst *GEP1 =
      dyn_cast<GetElementPtrInst>(L1->getPointerOperand());
    const GetElementPtrInst *GEP2 =
      dyn_cast<GetElementPtrInst>(L2->getPointerOperand());

    if(GEP1 == GEP2) return MayAlias;
    if(!GEP1 || !GEP2) return MayAlias;

    Type *T1 = GEP1->getPointerOperand()->getType();
    Type *T2 = GEP2->getPointerOperand()->getType();

    if(T1 != T2) return MayAlias;

    if(!TAA->isSane(T1)) return MayAlias;

    StructType *SB = 0;
    const ConstantInt *CI = 0;
    if(!NEFAA->isFieldPointer(GEP1, &SB, &CI))
      return MayAlias;

    if(!NEFAA->isFieldPointer(GEP2, &SB, &CI))
      return MayAlias;

    if(NEFAA->captured(GEP1) || NEFAA->captured(GEP2)) return MayAlias;

    if(!fieldsDiffer(GEP1, GEP2)) return MayAlias;

    const Structs S1 = getAffectedStructs(GEP1);
    const Structs S2 = getAffectedStructs(GEP2);

    for(unsigned i = 0; i < S1.size(); ++i) {
      if(jointTypes.count(S1[i])) {
        DEBUG(errs() << S1[i]->getName() << " sucks!\n");
        DEBUG(errs() << "Done: " << *V1 << "\n" << *V2 << "\n\n");
        return MayAlias;
      }
    }

    for(unsigned i = 0; i < S2.size(); ++i) {
      if(jointTypes.count(S2[i])) {
        DEBUG(errs() << S2[i]->getName() << " sucks!\n");
        DEBUG(errs() << "Done: " << *V1 << "\n" << *V2 << "\n\n");
        return MayAlias;
      }
    }

    DEBUG(errs()
          << "Disjoint:\n"
          << *V1 << "\n"
          << *V2 << "\n\n");

    ++numNoAliases;
    return NoAlias;
  }

  StringRef getLoopAAName() const {
    return "disjoint-fields-aa";
  }

  /// getAdjustedAnalysisPointer - This method is used when a pass implements an
  /// analysis interface through multiple inheritance.  If needed, it should
  /// override this to adjust the this pointer as needed for the specified pass
  /// info.
  void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA*)this;
    return this;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<liberty::TypeSanityAnalysis>();
    AU.addRequired<liberty::NonCapturedFieldsAnalysis>();
    LoopAA::getAnalysisUsage(AU);
    AU.setPreservesAll();
  }
};

char DisjointFieldsAA::ID = 0;

static RegisterPass<DisjointFieldsAA>
X("disjoint-fields-aa", "Prove fields of uncaptured sane types are disjoint", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);
