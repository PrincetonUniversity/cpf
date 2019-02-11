#ifndef LLVM_LIBERTY_REMEDIATOR_SELECTOR_H
#define LLVM_LIBERTY_REMEDIATOR_SELECTOR_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"

#include <vector>
#include <set>
#include <map>

#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/SpecPriv/Selector.h"
#include "liberty/SpecPriv/TXIORemed.h"
#include "liberty/SpecPriv/ControlSpecRemed.h"
#include "liberty/SpecPriv/ReduxRemed.h"
#include "liberty/SpecPriv/SmtxSlampRemed.h"
//#include "liberty/SpecPriv/ReplicaRemed.h"
#include "liberty/SpecPriv/CommutativeLibsRemed.h"
//#include "liberty/SpecPriv/CommutativeGuessRemed.h"
//#include "liberty/SpecPriv/PureFunRemed.h"
#include "liberty/SpecPriv/Remediator.h"

//#include "Classify.h"
//#include "LocalityAA.h"
//#include "PtrResidueAA.h"

namespace liberty
{
namespace SpecPriv
{

struct RemedSelector : public ModulePass, public Selector
{
  static char ID;
  RemedSelector() : ModulePass(ID), Selector() {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

  StringRef getPassName() const { return "remed-selector"; }

  // Update on clone
  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap);

  // Isn't multiple inheritance wonderful!?
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI)
  {
    if(PI == &RemedSelector::ID)
      return (RemedSelector*)this;
    else if(PI == &Selector::ID)
      return (Selector*)this;
    return this;
  }

protected:
  // The runtime gives each worker an isolated memory space
  virtual bool pipelineOption_ignoreAntiOutput() const { return true; }

  // Called after a late inlining
  virtual void resetAfterInline(
    Instruction *callsite_no_longer_exists,
    Function *caller,
    Function *callee,
    const ValueToValueMapTy &vmap,
    const CallsPromotedToInvoke &call2invoke);

  virtual Pass &getPass() { return *this; }
};


}
}

#endif

