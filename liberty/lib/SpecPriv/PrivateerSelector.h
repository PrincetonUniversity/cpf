// Given a set of hot loops with assignments,
// decide which loops to parallelize, and
// form a compatible assignment.
#ifndef LLVM_LIBERTY_SPEC_PRIV_PRIVATEER_SELECTOR_H
#define LLVM_LIBERTY_SPEC_PRIV_PRIVATEER_SELECTOR_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"

#include <vector>
#include <set>
#include <map>

#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/SpecPriv/Selector.h"

#include "Classify.h"
#include "LocalityAA.h"
#include "PtrResidueAA.h"

namespace liberty
{
namespace SpecPriv
{

struct SpecPrivSelector : public ModulePass, public Selector
{
  static char ID;
  SpecPrivSelector() : ModulePass(ID), Selector(), assignment() {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

  StringRef getPassName() const { return "specpriv-selector"; }

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
    if(PI == &SpecPrivSelector::ID)
      return (SpecPrivSelector*)this;
    else if(PI == &Selector::ID)
      return (Selector*)this;
    return this;
  }

protected:
  // Select from hot loops with a heap assignment
  virtual void computeVertices(Vertices &vertices);
  // Loops with compatible heap assignments
  virtual bool compatibleParallelizations(const Loop *A, const Loop *B) const;
  // Locality, Prediction, Control, Pointer-residue speculations.
  virtual ControlSpeculation *getControlSpeculation() const;
  virtual PredictionSpeculation *getPredictionSpeculation() const;
  virtual void buildSpeculativeAnalysisStack(const Loop *A);
  virtual void destroySpeculativeAnalysisStack();
  // Called after a late inlining
  virtual void resetAfterInline(
    Instruction *callsite_no_longer_exists,
    Function *caller,
    Function *callee,
    const ValueToValueMapTy &vmap,
    const CallsPromotedToInvoke &call2invoke);

  virtual Pass &getPass() { return *this; }

private:
  PtrResidueAA *residueaa;
  PredictionAA *predaa;
  EdgeCountOracle *edgeaa;
  LocalityAA *localityaa;

  HeapAssignment assignment;
};


}
}

#endif

