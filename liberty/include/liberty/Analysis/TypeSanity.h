#ifndef LLVM_LIBERTY_TYPEAAPASS
#define LLVM_LIBERTY_TYPEAAPASS

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/DenseSet.h"

#include "liberty/Analysis/ClassicLoopAA.h"

namespace liberty {
  using namespace llvm;

  /* A pass which identifies insane types,
   * thus implicitly identifying sane types.
   * This is NOT a LoopAA; see instead TypeAA in lib/Analysis/TypeAA.cpp
   */
  class TypeSanityAnalysis : public ModulePass {

  public:
    typedef DenseSet<Type*> Types;

  private:
    Module *currentMod;

    Types     insane;

    bool addInsane(Type *);
    bool runOnFunction(Function &);
    void runOnGlobalVariable(GlobalVariable &gv);

  public:

    static char ID;
    TypeSanityAnalysis()
      : ModulePass(ID) {}

    void getAnalysisUsage(AnalysisUsage &au) const
    {
      au.setPreservesAll();
    }

    bool runOnModule(Module &);

    StringRef getPassName() const
    {
      return "Identify sane types";
    }

    static Type *getBaseType(Type *);

    bool isSane(Type *) const;

    // Conservatively determine if an allocation unit of type
    // 'container' may contain an allocation unit of type 'element'.
    bool typeContainedWithin(Type *container, Type *element) const;

  };
}


#endif // LLVM_LIBERTY_TYPEAAPASS

