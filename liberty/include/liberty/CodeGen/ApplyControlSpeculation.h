// Modifies the code before parallelization.
// - Validate control speculation.
#ifndef LLVM_LIBERTY_SPECPRIV_APPLY_CONTROL_SPECULATION_H
#define LLVM_LIBERTY_SPECPRIV_APPLY_CONTROL_SPECULATION_H

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


struct ApplyControlSpec : public ModulePass
{
  static char ID;
  ApplyControlSpec() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  typedef std::set<const Value*> VSet;

  Module *mod;
  std::vector<Loop*> loops;

  void init(ModuleLoops &mloops);

  bool applyControlSpec(ModuleLoops &mloops);
  bool applyControlSpecToLoop(
      const BasicBlock *loop_header,
      std::set<std::pair<TerminatorInst *, unsigned>> &processed,
      ModuleLoops &mloops);
};

}
}


#endif

