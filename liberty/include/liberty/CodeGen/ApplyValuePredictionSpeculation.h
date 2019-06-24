// Modifies the code before parallelization.
// - Validate that speculated values are constant across loop iterations
#ifndef LLVM_LIBERTY_SPECPRIV_APPLY_VALUE_PREDICTION_SPECULATION_H
#define LLVM_LIBERTY_SPECPRIV_APPLY_VALUE_PREDICTION_SPECULATION_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/InstInsertPt.h"

#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Recovery.h"
#include "liberty/CodeGen/RoI.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


struct ApplyValuePredSpec : public ModulePass
{
  static char ID;
  ApplyValuePredSpec() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  typedef std::set<const Value*> VSet;

  Module *mod;
  Type *voidty, *voidptr;
  IntegerType *u8, *u16, *u32, *u64;
  std::vector<Loop*> loops;

  void init(ModuleLoops &mloops);

  bool runOnLoop(Loop *loop);
  bool addValueSpecChecks(Loop *loop);

  bool manageMemOps(Loop *loop);

};

}
}


#endif

