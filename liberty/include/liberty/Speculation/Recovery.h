// Responsible for creating misspeculation recovery functions.
#ifndef LLVM_LIBERTY_SPEC_PRIV_RECOVERY_H
#define LLVM_LIBERTY_SPEC_PRIV_RECOVERY_H

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/IR/Type.h"

#include "liberty/Utilities/ModuleLoops.h"


namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

// Represents a liveout structure.
struct LiveoutStructure
{
  typedef std::vector<Instruction*> IList;
  typedef std::vector<PHINode*> PhiList;
  typedef std::vector<GlobalVariable*> ReduxGVList;

  IList liveouts;
  PhiList phis;

  IList reduxLiveouts;

  StructType *type;
  Instruction *object;

  IList reduxObjects;

  void replaceAllUsesOfWith(Value *oldv, Value *newv);
  void print(raw_ostream &fout) const;
};

// Represents a recovery function
// corresponding to a given loop.
struct RecoveryFunction
{
  // The recovery function;
  // contains nothing but the loop.
  Function    *fcn;

  // The first two parameters are
  // a [low,high] iteration range.
  // Iterations are numbered from zero.

  typedef std::vector<Value*> VList;

  // Next, we have the initial values
  // for zero or more loop-carried register
  // deps.
  LiveoutStructure liveoutStructure;

  // Finally, there are zero or more live-in
  // values.
  // This list contains references to
  // live-in values from the original
  // function.
  VList liveins;

  // Not true anymore: There are never live-out values,
  // since those are demoted to private
  // memory by the preprocessor.
  //
  // Preprocessor does not demote all lc regs to memory.
  // There are zero or more live-out
  // values, the reducible live-outs(reduxLiveouts, found in the
  // liveoutStructure)

  // Finally, that function returns
  // an integer code.
  //  (0) ==> the last evaluated iteration
  //          took a backedge to continue looping.
  //  (k) ==> the last evaluated iteration
  //          took exit edge N.

  // We need a canonical numbering for all
  // exiting edges.
  typedef std::pair<TerminatorInst*,unsigned> CtrlEdge;
  typedef std::map<CtrlEdge,unsigned> CtrlEdgeNumbers;
  typedef std::map<CtrlEdge,BasicBlock*> CtrlEdgeDestinations;

  CtrlEdgeNumbers exitNumbers;
  CtrlEdgeDestinations exitDests;

  void replaceAllUsesOfWith(Value *oldv, Value *newv);

  void print(raw_ostream &fout) const;
  void dump() const;
};



struct Recovery
{
  /*
  Recovery()
  {
    LLVMContext &ctx = getGlobalContext();
    u32 = Type::getInt32Ty(ctx);
    u64 = Type::getInt64Ty(ctx);
  }
  */

  RecoveryFunction &getRecoveryFunction(Loop *loop, ModuleLoops &mloops, const LiveoutStructure &liveouts);
  const RecoveryFunction &getRecoveryFunction(Loop *loop) const;

  // Update the recovery function records with a value
  // replacement.
  void replaceAllUsesOfWith(Value *oldv, Value *newv);

  void print(raw_ostream &fout) const;
  void dump() const;

private:
  //IntegerType *u32, *u64;

  typedef std::map<BasicBlock*, RecoveryFunction> Loop2Recovery;
  Loop2Recovery recoveryFunctions;
};

}
}


#endif

