#ifndef LLVM_LIBERTY_ITERATION_PRIVATIZATION_H
#define LLVM_LIBERTY_ITERATION_PRIVATIZATION_H

#include "llvm/Pass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/DominanceFrontier.h"

#include "liberty/Exclusions/Exclusions.h"

namespace liberty
{
  using namespace llvm;

  class IterationPrivatization : public FunctionPass
  {
  public:
    typedef std::vector<Value*> Privatized;
    typedef Privatized::const_iterator iterator;

  private:
    typedef DenseMap<BasicBlock*,Privatized> Loop2Priv;

    Loop2Priv privatized;

  public:
    static char ID;
    IterationPrivatization() : FunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &au) const
    {
      au.addRequired< AAResultsWrapperPass>();

      au.addRequired< LoopInfoWrapperPass >();
      au.addRequired< DominatorTreeWrapperPass >();
      au.addRequired< DominanceFrontierWrapperPass >();
      au.addRequired< ScalarEvolutionWrapperPass >();

      au.addPreserved< LoopInfoWrapperPass >();
      au.addPreserved< DominatorTreeWrapperPass >();
      au.addPreserved< DominanceFrontierWrapperPass >();
    }

    bool runOnFunction(Function &F);

    iterator begin(Loop *loop) const;
    iterator end(Loop *loop) const;


    typedef std::set< LoadInst *> Loads;
    typedef DenseMap<Value*,Value*>  LoadBases;

    void identifyPrivatizableStuff(AliasAnalysis &AA, const DataLayout &TD, ScalarEvolution &scev, Loop *loop, Loads &scalarsToPrivatize, Loads &aggregatesToPrivatize, LoadBases &loadBases) const;


    static Value *getBase(LoadBases &loadBases, const SCEV *s);
    static const SCEV *getScev(ScalarEvolution &scev, Loop *loop, Value *ptr);

  protected:
    bool runOnLoop(Function *f, LoopInfo &loopInfo, Loop *loop);
  };

}

#endif //LLVM_LIBERTY_ITERATION_PRIVATIZATION_H

