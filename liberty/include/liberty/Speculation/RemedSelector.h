#ifndef LLVM_LIBERTY_REMEDIATOR_SELECTOR_H
#define LLVM_LIBERTY_REMEDIATOR_SELECTOR_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/CallGraph.h"

#include <vector>
#include <set>
#include <map>

#include "scaf/SpeculationModules/EdgeCountOracleAA.h"
#include "scaf/SpeculationModules/PredictionSpeculation.h"
#include "liberty/Speculation/Selector.h"
#include "scaf/SpeculationModules/TXIOAA.h"
#include "scaf/SpeculationModules/ControlSpecRemed.h"
#include "scaf/SpeculationModules/ReduxRemed.h"
#include "scaf/SpeculationModules/CommutativeLibsAA.h"
#include "scaf/SpeculationModules/Remediator.h"
#include "liberty/Speculation/UpdateOnCloneAdaptors.h"
#include "liberty/Speculation/Classify.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm::noelle;

struct RemedSelector : public ModulePass, public Selector
{
  static char ID;
  RemedSelector() : ModulePass(ID), Selector() {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

  StringRef getPassName() const { return "remed-selector"; }

  virtual const HeapAssignment &getAssignment() const;
  virtual HeapAssignment &getAssignment();

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
  virtual void computeVertices(Vertices &vertices);
  // Loops with compatible heap assignments
  virtual bool compatibleParallelizations(const Loop *A, const Loop *B) const;

  // The runtime gives each worker an isolated memory space
  //virtual bool pipelineOption_ignoreAntiOutput() const { return true; }

  // Called after a late inlining
  virtual void resetAfterInline(
    Instruction *callsite_no_longer_exists,
    Function *caller,
    Function *callee,
    const ValueToValueMapTy &vmap,
    const CallsPromotedToInvoke &call2invoke);

  virtual Pass &getPass() { return *this; }

private:
  HeapAssignment assignment;
};

}
}

#endif

