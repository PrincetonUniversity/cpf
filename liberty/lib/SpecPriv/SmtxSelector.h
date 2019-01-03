// Given a set of hot loops with assignments,
// decide which loops to parallelize, and
// form a compatible assignment.
#ifndef LLVM_LIBERTY_SPEC_PRIV_SMTX_SELECTOR_H
#define LLVM_LIBERTY_SPEC_PRIV_SMTX_SELECTOR_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"

#include <vector>
#include <set>
#include <map>

#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/SpecPriv/PipelineStrategy.h"
#include "liberty/SpecPriv/Selector.h"
#include "liberty/SLAMP/SlampOracleAA.h"

#include "Ebk.h"
#include "SmtxAA.h"

namespace liberty
{
namespace SpecPriv
{

struct SmtxSelector : public ModulePass, public Selector
{
  static char ID;
  SmtxSelector() : ModulePass(ID), Selector() {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

  // Isn't multiple inheritance wonderful!?
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI)
  {
    if(PI == &SmtxSelector::ID)
      return (SmtxSelector*)this;
    else if(PI == &Selector::ID)
      return (Selector*)this;
    return this;
  }

protected:
  // The runtime gives each worker an isolated memory space
  virtual bool pipelineOption_ignoreAntiOutput() const { return true; }
  // Control
  virtual ControlSpeculation *getControlSpeculation() const;
  virtual PredictionSpeculation *getPredictionSpeculation() const;
  virtual void buildSpeculativeAnalysisStack(const Loop *A);
  virtual void destroySpeculativeAnalysisStack();
  virtual void resetAfterInline(
    Instruction *callsite_no_longer_exists,
    Function *caller,
    Function *callee,
    const ValueToValueMapTy &vmap,
    const CallsPromotedToInvoke &call2invoke);
  virtual Pass &getPass() { return *this; }

private:
  // Holds our speculative analysis adaptors.
  SlampOracle* slampaa;
  SmtxAA *smtxaa;
  EdgeCountOracle *edgeaa;
};


}
}

#endif

