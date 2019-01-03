// Given a set of hot loops with assignments,
// decide which loops to parallelize, and
// form a compatible assignment.
#ifndef LLVM_LIBERTY_SMTX2_SELECTOR_H
#define LLVM_LIBERTY_SMTX2_SELECTOR_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"

#include <vector>
#include <set>
#include <map>

#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/SpecPriv/UpdateOnClone.h"
#include "liberty/SpecPriv/PipelineStrategy.h"
#include "liberty/SpecPriv/Selector.h"

#include "Classify.h"
#include "Ebk.h"
#include "LocalityAA.h"
#include "PtrResidueAA.h"
#include "SmtxAA.h"


namespace liberty
{
namespace SpecPriv
{

struct Smtx2Selector : public ModulePass, public Selector
{
  static char ID;
  Smtx2Selector() : ModulePass(ID), Selector(), assignment() {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

  StringRef getPassName() const { return "smtx2-selector"; }

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
    if(PI == &Smtx2Selector::ID)
      return (Smtx2Selector*)this;
    else if(PI == &Selector::ID)
      return (Selector*)this;
    return this;
  }

protected:
  // Select from hot loops with a heap assignment
  virtual void computeVertices(Vertices &vertices);
  // Loops with compatible heap assignments
  virtual bool compatibleParallelizations(const Loop *A, const Loop *B) const;
  // Loality, Prediction, Control, Pointer-residue and smtx(2) speculations
  virtual ControlSpeculation *getControlSpeculation() const;
  virtual PredictionSpeculation *getPredictionSpeculation() const;
  virtual void buildSpeculativeAnalysisStack(const Loop *A);
  virtual void destroySpeculativeAnalysisStack();

  virtual Pass &getPass() { return *this; }
  virtual void resetAfterInline(
    Instruction *callsite_no_longer_exists,
    Function *caller,
    Function *callee,
    const ValueToValueMapTy &vmap,
    const CallsPromotedToInvoke &call2invoke);

private:
  // Holds our speculative analysis adaptors.
  SmtxAA *smtxaa;
  PtrResidueAA *residueaa;
  PredictionAA *predaa;
  EdgeCountOracle *edgeaa;
  LocalityAA *localityaa;

  HeapAssignment assignment;
};


}
}

#endif

