#ifndef PURE_LOOP_VARIANT_ALLOCATION_H
#define PURE_LOOP_VARIANT_ALLOCATION_H

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
class LoopVariantAllocation : public llvm::ModulePass, public liberty::ClassicLoopAA {
  const DataLayout *DL;
  const TargetLibraryInfo *tli;
public:
  static char ID;

  LoopVariantAllocation();

  virtual bool runOnModule(llvm::Module &M);

  virtual ModRefResult getModRefInfo(llvm::CallSite CS1,
                                     TemporalRelation Rel,
                                     llvm::CallSite CS2,
                                     const llvm::Loop *L);

  virtual ModRefResult getModRefInfo(llvm::CallSite CS,
                                     TemporalRelation Rel,
                                     const Pointer &P,
                                     const llvm::Loop *L);

  virtual AliasResult aliasCheck(const Pointer &P1,
                                 TemporalRelation Rel,
                                 const Pointer &P2,
                                 const Loop *L);

  StringRef getLoopAAName() const { return "loop-variant-allocation-aa"; }

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const;

  virtual void *getAdjustedAnalysisPointer(llvm::AnalysisID PI);
};

}

#endif /* PURE_FUN_AA_H */
