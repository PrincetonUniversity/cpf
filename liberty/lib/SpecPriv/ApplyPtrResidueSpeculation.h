// Modifies the code before parallelization.
// - Add validation for pointer-residue speculation
#ifndef LLVM_LIBERTY_SPECPRIV_APPLY_POINTER_RESIDUE_SPEC_H
#define LLVM_LIBERTY_SPECPRIV_APPLY_POINTER_RESIDUE_SPEC_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/InstInsertPt.h"

#include "Classify.h"
#include "Recovery.h"
#include "RoI.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


struct ApplyPtrResidueSpec : public ModulePass
{
  static char ID;
  ApplyPtrResidueSpec() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  typedef std::set<const Value*> VSet;

  Module *mod;
  Type *voidty, *voidptr;
  IntegerType *u16;
  std::vector<Loop*> loops;

  void init(ModuleLoops &mloops);

  bool runOnLoop(Loop *loop);

  bool addPtrResidueChecks(Loop *loop);
  bool manageMemOps(Loop *loop);

};

}
}


#endif

