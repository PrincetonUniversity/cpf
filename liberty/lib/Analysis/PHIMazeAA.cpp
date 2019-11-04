#define DEBUG_TYPE "phi-maze-aa"

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/ValueTracking.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"

using namespace llvm;

class PHIMazeAA : public ModulePass, public liberty::ClassicLoopAA {

  const DataLayout* DL;
  typedef DenseSet<const Instruction *> InstSet;

  bool getDefs(const Value *V, InstSet &defSet) {

    const Instruction *I = dyn_cast<Instruction>(V);
    if(!I) return false;

    if(defSet.count(I))
      return true;

    defSet.insert(I);

    if(isNoAliasCall(I))
      return true;

    if(const PHINode *phi = dyn_cast<PHINode>(I)) {

      for(unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
        if(!getDefs(phi->getIncomingValue(i), defSet))
          return false;
      }
      return true;
    }

    if(const CastInst *cast = dyn_cast<CastInst>(I))
      return getDefs(cast->getOperand(0), defSet);

    if(const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(I))
      return getDefs(gep->getPointerOperand(), defSet);

    if(isa<AllocaInst>(I))
      return true;

    return false;
  }

public:
  static char ID;
  PHIMazeAA() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
    DL = &M.getDataLayout();
    InitializeLoopAA(this, *DL);
    return false;
  }

  virtual AliasResult
  aliasCheck(const Pointer &P1, TemporalRelation Rel, const Pointer &P2,
             const Loop *L, Remedies &R,
             DesiredAliasResult dAliasRes = DNoOrMustAlias) {

    if (dAliasRes == DMustAlias)
      return MayAlias;

    const Value *V1 = P1.ptr, *V2 = P2.ptr;

    InstSet defSet1;
    bool isValid1 = getDefs(V1, defSet1);

    InstSet defSet2;
    bool isValid2 = getDefs(V2, defSet2);

    if(isValid1 && isValid2) {
      const Instruction *I1 = cast<Instruction>(V1);
      const Instruction *I2 = cast<Instruction>(V2);

      if(I1->getParent()->getParent() != I2->getParent()->getParent())
        return MayAlias;

      typedef InstSet::iterator InstSetIt;
      for(InstSetIt inst = defSet1.begin(); inst != defSet1.end(); ++inst) {
        if(defSet2.count(*inst))
          return MayAlias;
      }

      return NoAlias;
    }

    const bool sameFun =
      P1.inst && P2.inst && P1.inst->getParent() == P2.inst->getParent();

    const Value *UO1 = GetUnderlyingObject(V1, *DL);
    const Value *UO2 = GetUnderlyingObject(V2, *DL);

    if(isValid1 && isa<GlobalVariable>(UO2))
      return NoAlias;

    if(isValid1 && isa<Argument>(UO2) && sameFun)
      return NoAlias;

    if(isa<GlobalVariable>(UO1) && isValid2)
      return NoAlias;

    if(isa<Argument>(UO1) && isValid2 && sameFun)
      return NoAlias;

    return MayAlias;
  }

  StringRef getLoopAAName() const {
    return "phi-maze-aa";
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

static RegisterPass<PHIMazeAA>
X("phi-maze-aa", "Search through a maze of local defs", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char PHIMazeAA::ID = 0;
