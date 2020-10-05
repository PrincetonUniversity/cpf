#define DEBUG_TYPE "semi-local-fun-aa"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/PureFunAA.h"
#include "liberty/Analysis/SemiLocalFunAA.h"
#include "scaf/Utilities/GetSize.h"


namespace liberty {

using namespace llvm;

  bool SemiLocalFunAA::isSemiLocalProp(const Instruction *inst) {
    return true;
  }

  bool SemiLocalFunAA::isSemiLocal(const Function *fun, const PureFunAA &pureFun) const {
    return pureFun.isRecursiveProperty(fun, semiLocalSet, globalSet,
                                       semiLocalFunSet, isSemiLocalProp);
  }

  void SemiLocalFunAA::runOnSCC(const PureFunAA::SCC &scc, PureFunAA &pureFun) {

    PureFunAA::SCCNum sccNum = pureFun.getSCCNum(scc);
    if(sccNum == ~0U)
      return;

    for(PureFunAA::SCCIt it = scc.begin(); it != scc.end(); ++it) {
      const Function *fun = (*it)->getFunction();
      if(fun && !isSemiLocal(fun, pureFun)) {
        globalSet.insert(sccNum);
      }
    }

    if(!globalSet.count(sccNum)) {
      semiLocalSet.insert(sccNum);
    }
  }

  void SemiLocalFunAA::initGlobalMod(const Value *v,
                            GlobalSet &mods, GlobalSet &refs,
                            FuncSet &funcs) {

    const User *user = dyn_cast<User>(v);
    if(!user)
      return;

    if(const Function *fun = dyn_cast<Function>(user)) {
      initGlobalMod(fun, mods, refs, funcs);
      return;
    }

    if(const GlobalVariable *global = dyn_cast<GlobalVariable>(user)) {
      mods.insert(global);
      return;
    }

    if(const GlobalAlias *alias = dyn_cast<GlobalAlias>(user)) {
      initGlobalMod(alias->getAliasee(), mods, refs, funcs);
      return;
    }

    typedef User::const_op_iterator OpIt;
    for(OpIt op = user->op_begin(); op != user->op_end(); ++op) {
      if(!isa<Instruction>(op))
        initGlobalMod(*op, mods, refs, funcs);
    }
  }

  void SemiLocalFunAA::initGlobalMod(const Function *fun,
                            GlobalSet &mods, GlobalSet &refs,
                            FuncSet &funcs) {

    if(funcs.count(fun))
      return;

    funcs.insert(fun);

    if(fun->isDeclaration())
      return;

    for(const_inst_iterator inst = inst_begin(fun); inst != inst_end(fun); ++inst) {
      initGlobalMod(&*inst, mods, refs, funcs);
    }
  }

  LoopAA::ModRefResult SemiLocalFunAA::getModRefInfo(const ImmutableCallSite CS, const unsigned argNo) {

    const Function *fun = CS.getCalledFunction();
    if( fun ) {
      Formal F = { fun->getName(), argNo };
      if(readOnlyFormalSet.count(F))
        return Ref;

      if (fun->hasParamAttribute(argNo, Attribute::ReadOnly) ||
          fun->hasParamAttribute(argNo, Attribute::ReadNone) ||
          fun->hasParamAttribute(argNo, Attribute::ByVal))
        return Ref;

      if( writeOnlyFormalSet.count(F))
        return Mod;
    }

    return ModRef;
  }

  LoopAA::ModRefResult
  SemiLocalFunAA::aliasedArgumentsModRef(const ImmutableCallSite CS,
                                         const Value *P, const unsigned Size,
                                         Remedies &R) const {

    LoopAA *aa = getTopAA();
    assert(aa && "Cogito ergo sum.");

    ModRefResult result = NoModRef;

    const DataLayout *TD = getDataLayout();

    const Function *fun = CS.getCalledFunction();
    assert(fun);

    for(unsigned i = 0; i < CS.arg_size(); ++i) {
      const Value *arg = CS.getArgument(i);

      if(arg->getType()->isPointerTy()) {

        const int argSize = liberty::getTargetSize(arg, TD);
        if(!fun->hasParamAttribute(i, Attribute::NoAlias) &&
           aa->alias(P, Size, Same, arg, argSize, NULL, R)) {
          result =  ModRefResult(result | getModRefInfo(CS, i));
        }
      }
    }

    return result;
  }

  bool SemiLocalFunAA::globalsAlias(const GlobalSet globals, const Value *P,
                                    const unsigned Size, Remedies &R) const {

    LoopAA *aa = getTopAA();
    assert(aa && "Cogito ergo sum.");

    const DataLayout *TD = getDataLayout();

    typedef GlobalSet::const_iterator GlobalSetIt;
    for(GlobalSetIt global = globals.begin(); global != globals.end(); ++global) {

      const int globalSize = liberty::getTargetSize(*global, TD);
      if(aa->alias(P, Size, Same, *global, globalSize, NULL, R)) {
        return true;
      }
    }

    return false;
  }

  SemiLocalFunAA::SemiLocalFunAA() : ModulePass(ID) {
    if(!semiLocalFunSet.size()) {

      for(int i = 0; !PureFunAA::pureFunNames[i].empty(); ++i) {
        semiLocalFunSet.insert(PureFunAA::pureFunNames[i]);
      }

      for(int i = 0; !PureFunAA::localFunNames[i].empty(); ++i) {
        semiLocalFunSet.insert(PureFunAA::localFunNames[i]);
      }

      for(int i = 0; !semiLocalFunNames[i].empty(); ++i) {
        semiLocalFunSet.insert(semiLocalFunNames[i]);
      }
    }

    if(!readOnlyFormalSet.size()) {
      for(int i = 0; readOnlyFormals[i].funName.size(); ++i) {
        readOnlyFormalSet.insert(readOnlyFormals[i]);
      }
    }
    if(!writeOnlyFormalSet.size()) {
      for(int i = 0; writeOnlyFormals[i].funName.size(); ++i) {
        writeOnlyFormalSet.insert(writeOnlyFormals[i]);
      }
    }


  }

  bool SemiLocalFunAA::runOnModule(Module &M) {
    const DataLayout &DL = M.getDataLayout();
    InitializeLoopAA(this, DL);

    PureFunAA &pureFun = getAnalysis<PureFunAA>();

    CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
    for (scc_iterator<CallGraph*> CGI = scc_begin(&CG), E = scc_end(&CG);
         CGI != E; ++CGI) {
      runOnSCC(*CGI, pureFun);
    }

    FuncSet funcs;

    typedef Module::const_iterator ModuleIt;
    for(ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
      LLVM_DEBUG(errs() << "SemiLocalFunAA: " << fun->getName());
      const Function *funP = &*fun;
      if(isSemiLocal(funP, pureFun)) {
        GlobalSet &mods = globalMod[funP];
        GlobalSet &refs = globalRef[funP];
        initGlobalMod(funP, mods, refs, funcs);
        funcs.clear();

        LLVM_DEBUG(
          errs() << " mods: ";
          for(GlobalSet::iterator i=mods.begin(), e=mods.end(); i!=e; ++i)
            errs() << (*i)->getName() << ", ";
          errs() << '\n';
          errs() << " refs: ";
          for(GlobalSet::iterator i=refs.begin(), e=refs.end(); i!=e; ++i)
            errs() << (*i)->getName() << ", ";
          errs() << '\n';

        );
      } else {
        LLVM_DEBUG(errs() << " is not semi-local\n");
      }
    }

    return false;
  }

  bool SemiLocalFunAA::readOnlyFormalArg(Formal &f) {
    return readOnlyFormalSet.count(f);
  }

  bool SemiLocalFunAA::writeOnlyFormalArg(Formal &f) {
    return writeOnlyFormalSet.count(f);
  }

  bool SemiLocalFunAA::readOnlyFormalArg(const Function *fcn, unsigned argno) {
    if (fcn->hasParamAttribute(argno, Attribute::ReadOnly) ||
        fcn->hasParamAttribute(argno, Attribute::ReadNone) ||
        fcn->hasParamAttribute(argno, Attribute::ByVal))
      return true;
    StringRef  name = fcn->getName();
    Formal f = {name,argno};
    return readOnlyFormalArg(f);
  }

  bool SemiLocalFunAA::writeOnlyFormalArg(const Function *fcn, unsigned argno) {
    StringRef  name = fcn->getName();
    Formal f = {name,argno};
    return writeOnlyFormalArg(f);
  }

  LoopAA::ModRefResult
  SemiLocalFunAA::getModRefInfo(CallSite CS1, TemporalRelation Rel,
                                CallSite CS2, const Loop *L, Remedies &R) {

    Remedies tmpR;

    // If one is local, the other semi-local and if their args do not alias,

    // then these two CallSites NoModRef.
    const Function *f1 = CS1.getCalledFunction();
    const Function *f2 = CS2.getCalledFunction();
    if(f1 && f2) {

      PureFunAA &pureFun = getAnalysis<PureFunAA>();
      if((pureFun.isLocal(f1) && isSemiLocal(f2, pureFun)) ||
         (pureFun.isLocal(f2) && isSemiLocal(f1, pureFun))) {

        LoopAA *aa = getTopAA();
        const DataLayout *TD = getDataLayout();

        // For each actual parameter of callsite 1 which is a pointer
        ModRefResult join = NoModRef;

        typedef CallSite::arg_iterator ArgIt;
        unsigned arg1no = 0;
        for(ArgIt i=CS1.arg_begin(), e=CS1.arg_end(); i!=e && join != ModRef; ++i, ++arg1no) {
          Value *arg1 = *i;
          if( !arg1->getType()->isPointerTy() )
            continue;

          const unsigned s1 = liberty::getTargetSize(arg1,TD);

          // For each actual parameter of callsite 2 which is a pointer
          for(ArgIt j=CS2.arg_begin(), f=CS2.arg_end(); j!=f; ++j) {
            Value *arg2 = *j;
            if( !arg2->getType()->isPointerTy() )
              continue;

            const unsigned s2 = liberty::getTargetSize(arg2,TD);

            // Do these pointers alias?
            if( aa->alias(arg1,s1, Rel, arg2,s2, L, tmpR) ) {
              // Yes.  What does this mean?
              Formal formal1 = { CS1.getCalledFunction()->getName(), arg1no };

              // Unless this is a write-only formal,
              // callsite 1 may read it.
              if( ! writeOnlyFormalArg(formal1) )
                join = ModRefResult(join | Ref);

              // Unless this is a read-only formal,
              // callsite 1 may write it.
              if( ! readOnlyFormalArg(formal1) )
                join = ModRefResult(join | Mod);

              // go on to the next actual of callsite 1
              break;
            }
          }
        }

        if (join != ModRef) {
          for (auto remed : tmpR)
            R.insert(remed);
        }
        return join;
      }
    }

    return ModRef;
  }

  LoopAA::ModRefResult
  SemiLocalFunAA::getModRefInfo(CallSite CS, TemporalRelation Rel,
                                const Pointer &P, const Loop *L, Remedies &R) {

    const Value *V = P.ptr;
    const unsigned Size = P.size;

    const Function *fun = CS.getCalledFunction();
    if(!fun)
      return ModRef;

    PureFunAA &pureFun = getAnalysis<PureFunAA>();
    if(!isSemiLocal(fun, pureFun))
      return ModRef;

    Remedies tmpR;

    ModRefResult result = aliasedArgumentsModRef(CS, V, Size, tmpR);
    if(result == ModRef)
      return ModRef;

    if(globalsAlias(globalMod[fun], V, Size, tmpR))
      return ModRef;
    else if( globalsAlias(globalRef[fun], V, Size, tmpR)) {
      for (auto remed : tmpR)
        R.insert(remed);
      return Ref;
    }

    if( !CS.getInstruction()->getType()->isVoidTy() ) {
      LoopAA *AA = getTopAA();
      if (AA->alias(CS.getInstruction(), Size, Rel, V, Size, L, tmpR) != NoAlias)
        return ModRef;
    }

    if (result != ModRef) {
      for (auto remed : tmpR)
        R.insert(remed);
    }

    return result;
  }

char SemiLocalFunAA::ID = 0;

StringRef  const SemiLocalFunAA::semiLocalFunNames[] = {
#include "SemiLocalFun.h"
""
};

const Formal SemiLocalFunAA::readOnlyFormals[] =  {
#include "ReadOnlyFormal.h"
  { "", 0 }
};

const Formal SemiLocalFunAA::writeOnlyFormals[] = {
#include "WriteOnlyFormal.h"
  { "", 0 }
};

PureFunAA::StringSet SemiLocalFunAA::semiLocalFunSet;
DenseSet<Formal> SemiLocalFunAA::readOnlyFormalSet;
DenseSet<Formal> SemiLocalFunAA::writeOnlyFormalSet;

static RegisterPass<SemiLocalFunAA>
X("semi-local-fun-aa", "Analysis on Semi-Local Functions", false, true);

static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

}
