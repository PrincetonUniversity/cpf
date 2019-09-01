#ifndef LLVM_LIBERTY_SEMI_LOCAL_FUN_AA_H
#define LLVM_LIBERTY_SEMI_LOCAL_FUN_AA_H

#include "llvm/ADT/StringExtras.h"

namespace liberty {

struct Formal {
  llvm::StringRef funName;
  unsigned argNo;
};
}

namespace llvm {
  template<> struct DenseMapInfo<liberty::Formal> {

    static bool isEqual(const liberty::Formal &F1, const liberty::Formal &F2) {
      return F1.argNo == F2.argNo && F1.funName == F2.funName;
    }

    static unsigned getHashValue(const liberty::Formal &F) {
      return
        HashString(F.funName) ^
        DenseMapInfo<unsigned>::getHashValue(F.argNo);
    }

    static liberty::Formal getEmptyKey() {
      liberty::Formal F = { "", 0 };
      return F;
    }

    static liberty::Formal getTombstoneKey() {
      liberty::Formal F = { "", static_cast<unsigned int>(~0) };
      return F;
    }
  };
}

namespace liberty {
class SemiLocalFunAA : public ModulePass, public liberty::ClassicLoopAA {

  typedef DenseSet<const Function *> FuncSet;
  typedef DenseSet<const GlobalVariable *> GlobalSet;
  typedef DenseMap<const Function *, GlobalSet> FuncToGlobalMap;

  FuncToGlobalMap globalMod;
  FuncToGlobalMap globalRef;

  PureFunAA::SCCNumSet semiLocalSet;
  PureFunAA::SCCNumSet globalSet;

  bool queryAnswersEnabled;

  static StringRef  const semiLocalFunNames[];
  static PureFunAA::StringSet semiLocalFunSet;

  static const Formal readOnlyFormals[];
  static const Formal writeOnlyFormals[];
  static DenseSet<Formal> readOnlyFormalSet;
  static DenseSet<Formal> writeOnlyFormalSet;

  static bool isSemiLocalProp(const Instruction *inst);

  void runOnSCC(const PureFunAA::SCC &scc, PureFunAA &pureFun);

  static void initGlobalMod(const Value *v,
                            GlobalSet &mods,
                            GlobalSet &refs,
                            FuncSet &funcs);
  static void initGlobalMod(const Function *fun,
                            GlobalSet &mods,
                            GlobalSet &refs,
                            FuncSet &funcs);

  static ModRefResult getModRefInfo(const ImmutableCallSite CS, const unsigned argNo);

  ModRefResult aliasedArgumentsModRef(const ImmutableCallSite CS,
                                      const Value *P, const unsigned Size,
                                      Remedies &R) const;

  bool globalsAlias(const GlobalSet globals, const Value *P,
                    const unsigned Size, Remedies &R) const;

public:

  static char ID;
  SemiLocalFunAA();
  bool runOnModule(Module &M);

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired<PureFunAA>();
    AU.addRequired<CallGraphWrapperPass>();
    AU.setPreservesAll();
  }

  bool isSemiLocal(const Function *fun, const PureFunAA &pureFun) const;

  static bool readOnlyFormalArg(Formal &f);

  static bool writeOnlyFormalArg(Formal &f);

  static bool readOnlyFormalArg(const Function *fcn, unsigned argno);

  static bool writeOnlyFormalArg(const Function *fcn, unsigned argno);

  ModRefResult getModRefInfo(CallSite CS1, TemporalRelation Rel, CallSite CS2,
                             const Loop *L, Remedies &R);

  ModRefResult getModRefInfo(CallSite CS, TemporalRelation Rel,
                             const Pointer &P, const Loop *L, Remedies &R);

  StringRef getLoopAAName() const {
    return "semi-local-fun-aa";
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

  void enableQueryAnswers() {
    queryAnswersEnabled = true;
  }
  void disableQueryAnswers() {
    queryAnswersEnabled = false;
  }
};


}


#endif

