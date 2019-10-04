#ifndef PURE_FUN_AA_H
#define PURE_FUN_AA_H

#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/CallGraph.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"

#include <set>
#include <vector>

namespace liberty
{

class PureFunAA : public llvm::ModulePass, public liberty::ClassicLoopAA {

  struct ltstr {
    bool operator()(const char* s1, const char* s2) const {
      return strcmp(s1, s2) < 0;
    }

    bool operator()(const StringRef s1, const StringRef s2) const {
      return s1.compare(s2) < 0;
    }


  };

 public:
  typedef unsigned SCCNum;
  typedef llvm::DenseSet<SCCNum> SCCNumSet;
  typedef std::vector<llvm::CallGraphNode *> SCC;
  typedef SCC::const_iterator SCCIt;
  typedef std::set<StringRef , ltstr> StringSet;

 private:

  typedef llvm::DenseMap<const llvm::Function *, SCCNum> FunToSCCMap;
  typedef FunToSCCMap::const_iterator FunToSCCMapIt;

  typedef bool (*Property)(const llvm::Instruction *inst);

  SCCNum sccCount;
  FunToSCCMap sccMap;

  SCCNumSet readOnlySet;
  SCCNumSet writeSet;

  SCCNumSet localSet;
  SCCNumSet globalSet;

 public:
  static StringRef  const pureFunNames[];
  static StringRef  const localFunNames[];
  static StringRef  const noMemFunNames[];

 private:
  static StringSet pureFunSet;
  static StringSet localFunSet;
  static StringSet noMemFunSet;

  void runOnSCC(const SCC &scc);

public:
  static
  bool argumentsAlias(const llvm::ImmutableCallSite CS1,
                      const llvm::ImmutableCallSite CS2,
                      liberty::LoopAA *aa,
                      const llvm::DataLayout *TD,
                      Remedies &R);

  static
  bool argumentsAlias(const llvm::ImmutableCallSite CS,
                      const llvm::Value *P,
                      const unsigned Size,
                      liberty::LoopAA *aa,
                      const llvm::DataLayout *TD,
                      Remedies &R);

  static char ID;

  PureFunAA();

  virtual bool runOnModule(llvm::Module &M);

  bool isReadOnly(const llvm::Function *fun) const;

  bool isLocal(const llvm::Function *fun) const;

  bool isPure(const llvm::Function *fun) const;

  SCCNum getSCCNum(const SCC &scc) const;
  SCCNum getSCCNum(const llvm::Function *fun) const;

  bool isRecursiveProperty(const llvm::Function *fun,
                           const SCCNumSet &trueSet,
                           const SCCNumSet &falseSet,
                           const StringSet &knownFunSet,
                           Property property) const;

  virtual ModRefResult getModRefInfo(llvm::CallSite CS1, TemporalRelation Rel,
                                     llvm::CallSite CS2, const llvm::Loop *L,
                                     Remedies &R);

  virtual ModRefResult getModRefInfo(llvm::CallSite CS, TemporalRelation Rel,
                                     const Pointer &P, const llvm::Loop *L,
                                     Remedies &R);

  StringRef getLoopAAName() const { return "pure-fun-aa"; }

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const;

  virtual void *getAdjustedAnalysisPointer(llvm::AnalysisID PI);
};

}
#endif /* PURE_FUN_AA_H */
