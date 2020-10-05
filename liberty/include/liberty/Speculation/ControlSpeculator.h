#ifndef LLVM_LIBERTY_SPEC_PRIV_EDGE_PROF_REFINE_H
#define LLVM_LIBERTY_SPEC_PRIV_EDGE_PROF_REFINE_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/ADT/SmallBitVector.h"

#include "scaf/Utilities/ModuleLoops.h"
#include "liberty/Speculation/UpdateOnClone.h"
#include "liberty/Analysis/ControlSpeculation.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

/// Use profiling information (llvm edge count profiles) to decide
/// when to speculate.
///
struct ProfileGuidedControlSpeculator : public ModulePass, public ControlSpeculation, public UpdateOnClone
{
  // CAUTION: multiple inheritance.  Don't grab a pointer to these
  // objects; instead call .getControlSpecPtr()

  static char ID;
  ProfileGuidedControlSpeculator() : ModulePass(ID) {}

  StringRef getPassName() const { return "Control Speculation Manager"; }

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod)
  {
    mloops = &getAnalysis< ModuleLoops >();
    return false;
  }

  // ------------------- CFG inspection methods

  // Determine if the provided control flow edge
  // is speculated to not run.
  virtual bool isSpeculativelyDead(const Instruction *term, unsigned succNo);

  // Determine if the given basic block is speculatively dead.
  virtual bool isSpeculativelyDead(const BasicBlock *bb);

  // speculatively dead edge sourcing from this term, rare misspec but observed
  // at least once in profiling
  virtual bool misspecInProfLoopExit(const Instruction *term);

  // ---------------------- for UpdateOnClone interface

  // Update on clone
  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap);

  bool dominatesTargetHeader(const BasicBlock* bb);

  void visit(const Function *fcn);

  virtual void reset();

  virtual void dot_block_label(const BasicBlock *bb, raw_ostream &fout);
  virtual void dot_edge_label(const Instruction *term, unsigned sn, raw_ostream &fout);

private:
  ModuleLoops *mloops;

  typedef std::map<const Instruction *, SmallBitVector> CtrlEdges;
  typedef std::set<const BasicBlock*> BlockSet;
  typedef std::set<const Function *> FcnSet;
  typedef std::set<const Instruction *> TermISet;

  struct LoopSpeculation
  {
    FcnSet visited;
    CtrlEdges deadEdges;
    TermISet misspecInProfLoopExits; // speculated dead loop exit but observed misspec in profiling
    BlockSet deadBlocks;
  };
  typedef std::map<const BasicBlock*,LoopSpeculation> PerLoopData;
  PerLoopData loops;
};


}
}

#endif

