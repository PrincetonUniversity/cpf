#ifndef PURE_STD_IN_OUT_ERR_AA_H
#define PURE_STD_IN_OUT_ERR_AA_H

#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"

namespace liberty
{
using namespace llvm;

/// This is deadline-quality, not perfectly sound analysis
/// which assumes that stdin, stdout, and stderr are only
/// every accessed via their global variables; they are never
/// captured, stored in data structures, etc.
class StdInOutErr : public llvm::ModulePass, public liberty::ClassicLoopAA {
public:
  static char ID;

  Module *Mod;

  StdInOutErr();

  virtual bool runOnModule(llvm::Module &M);

  virtual AliasResult aliasCheck(const Pointer &P1,
                                 TemporalRelation Rel,
                                 const Pointer &P2,
                                 const Loop *L);

  /// May not call down the LoopAA stack, but may top
  virtual ModRefResult getModRefInfo(CallSite CS1,
                                     TemporalRelation Rel,
                                     CallSite CS2,
                                     const Loop *L);

  /// V is never a CallSite
  /// May not call down the LoopAA stack, but may top
  virtual ModRefResult getModRefInfo(CallSite CS,
                                     TemporalRelation Rel,
                                     const Pointer &P,
                                     const Loop *L);


  virtual bool pointsToConstantMemory(const Value *v, const Loop *L);

  StringRef getLoopAAName() const { return "std-in-out-err-aa"; }

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const;

  virtual void *getAdjustedAnalysisPointer(llvm::AnalysisID PI);
};

}

#endif /* PURE_FUN_AA_H */
