#define DEBUG_TYPE "auto-restrict-aa"

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"

#include "liberty/Analysis/FindSource.h"

#include "liberty/Utilities/CallSiteFactory.h"

using namespace llvm;
using namespace liberty;

static const Function *getParentFunction(const Value *v) {

  if(const Instruction *inst = dyn_cast<Instruction>(v)) {
    return inst->getParent()->getParent();
  }

  if(const Argument *arg = dyn_cast<Argument>(v)) {
    return arg->getParent();
  }

  return NULL;
}

class AutoRestrictAA : public ModulePass, public liberty::ClassicLoopAA {

private:
  typedef const std::vector<CallGraphNode *> SCC;
  typedef SCC::const_iterator SCCIt;
  typedef DenseSet<const Function *> FuncSet;
  typedef Module::const_iterator ModuleIt;
  typedef Value::const_use_iterator UseIt;
  typedef Value::const_user_iterator UserIt;

  FuncSet tainted;
  FuncSet restricted;

  void recursiveTaint(const Function &fun) {
    for(const_inst_iterator inst = inst_begin(fun); inst != inst_end(fun); ++inst) {
      const CallSite CS =
	liberty::getCallSite(const_cast<Instruction *>(&*inst));
      if(CS.getInstruction()) {
        if(const Function *target = CS.getCalledFunction()) {
          if(!tainted.count(target)) {
            tainted.insert(target);
            recursiveTaint(fun);
          }
        }
      }
    }
  }

  AliasResult aliasCheck(const Argument *arg1, unsigned V1Size,
                         const Argument *arg2, unsigned V2Size, Remedies &R) {

    if(arg1 == arg2) {
      return MayAlias;
    }

    Remedies tmpR;

    const Function *fun   = getParentFunction(arg1);
    const Function *fun2  = getParentFunction(arg2);

    if(fun != fun2) {
      return MayAlias;
    }

    if(tainted.count(fun)) {
      return MayAlias;
    }

    LoopAA *aa = getTopAA();
    assert(aa && "Cogito ergo sum.");

    for(UserIt user = fun->user_begin(); user != fun->user_end(); ++user) {

      const CallSite CS =
        liberty::getCallSite(const_cast<User *>(*user));
      assert(CS.getInstruction()    && "This should be tainted.");
      assert(CS.getCalledFunction() && "This should be tainted.");

      const Value *callerArg1 = CS.getArgument(arg1->getArgNo());
      const Value *callerArg2 = CS.getArgument(arg2->getArgNo());

      assert(arg1 != callerArg1 && "No progress!");
      assert(arg2 != callerArg2 && "No progress!");

      AliasResult AR = aa->alias(callerArg1, V1Size,
                                 Same,
                                 callerArg2, V2Size,
                                 NULL, tmpR);
      if(AR != NoAlias) {
        return MayAlias;
      }
    }

    for (auto remed : tmpR)
      R.insert(remed);

    return NoAlias;
  }

  AliasResult aliasCheck(const Argument *arg, unsigned V1Size, const Value *V,
                         unsigned V2Size, Remedies &R) {

    const Function *fun = getParentFunction(arg);
    if(tainted.count(fun))
      return MayAlias;

    Remedies tmpR;

    LoopAA *aa = getTopAA();
    assert(aa && "Cogito ergo sum.");

    for(UserIt user = fun->user_begin(); user != fun->user_end(); ++user) {

      const CallSite CS =
        liberty::getCallSite(const_cast<User *>(*user));
      assert(CS.getCalledFunction() && "This should be tainted.");

      const Value *callerArg = CS.getArgument(arg->getArgNo());

      AliasResult AR = aa->alias(callerArg, V1Size,
                                 Same,
                                 V, V2Size,
                                 NULL, tmpR);
      if(AR != NoAlias)
        return MayAlias;
    }

    for (auto remed : tmpR)
      R.insert(remed);

    return NoAlias;
  }

  public:
  static char ID;
  AutoRestrictAA() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
    const DataLayout &DL = M.getDataLayout();
    InitializeLoopAA(this, DL);

    // Recursive functions taint
    CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
    for (scc_iterator<CallGraph*> CGI = scc_begin(&CG), E = scc_end(&CG);
         CGI != E; ++CGI) {
      SCC &scc = *CGI;
      if(scc.size() > 1) {
        for(SCCIt it = scc.begin(); it != scc.end(); ++it) {
          const Function *fun = (*it)->getFunction();
          tainted.insert(fun);
        }
      }
    }

    for(ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
      for(UserIt user = fun->user_begin(); user != fun->user_end(); ++user) {
        if(const Instruction *inst = dyn_cast<Instruction>(*user)) {
          if(inst->getParent()->getParent() == &*fun) {
            tainted.insert(&*fun);
          }
        }
      }
    }

    // Function pointers taint
    for(ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
      if(fun->hasAddressTaken()) {
        tainted.insert(&*fun);
      }
    }

    // Variadic functions taint
    for(ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
      if(fun->isVarArg()) {
        tainted.insert(&*fun);
      }
    }

    // Functions called by tainting functions taint
    for(ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
      if(tainted.count(&*fun)) {
        recursiveTaint(*fun);
      }
    }

    // Functions without local linkage taint, but not recursively
    if(!liberty::FULL_UNIVERSAL) {
      for(ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
        if(!fun->hasLocalLinkage()) {
          tainted.insert(&*fun);
        }
      }
    }

    // Print the restricted functions
    for(ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
      if(!tainted.count(&*fun)) {
        LLVM_LLVM_DEBUG(errs() << fun->getName() << " is restricted\n");
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
    const unsigned V1Size = P1.size, V2Size = P2.size;

    const Argument *arg1 = liberty::findArgumentSource(V1);
    const Argument *arg2 = liberty::findArgumentSource(V2);

    if(arg1 && arg2 && aliasCheck(arg1, V1Size, arg2, V2Size, R) == NoAlias) {
      return NoAlias;
    }

    if(arg1 && !arg2 && aliasCheck(arg1, V1Size, V2, V2Size, R) == NoAlias) {
      return NoAlias;
    }

    if(!arg1 && arg2 && aliasCheck(arg2, V2Size, V1, V1Size, R) == NoAlias) {
      return NoAlias;
    }

    return MayAlias;
  }

  StringRef getLoopAAName() const {
    return "auto-restrict-aa";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired<CallGraphWrapperPass>();
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

char AutoRestrictAA::ID = 0;

static RegisterPass<AutoRestrictAA>
X("auto-restrict-aa", "AA based on automatically derived restricted arguments", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);
