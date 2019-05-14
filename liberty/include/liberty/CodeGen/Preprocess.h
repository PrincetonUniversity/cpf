// Modifies the code before parallelization.
// - Duplicate functions so that there are
//   no side entrances to the parallel region
// - Update the parallel region so that live-out
//   register values are saved to a (private) memory
//   object
// - Create a recovery function
#ifndef LLVM_LIBERTY_SPECPRIV_PREPROCESS_H
#define LLVM_LIBERTY_SPECPRIV_PREPROCESS_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/InstInsertPt.h"

#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Recovery.h"
#include "liberty/CodeGen/RoI.h"

#include <set>
#include <unordered_set>

namespace liberty {
namespace SpecPriv {
using namespace llvm;

struct Preprocess : public ModulePass {
  static char ID;
  Preprocess() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

  Recovery &getRecovery() { return recovery; }
  const RecoveryFunction &getRecoveryFunction(Loop *loop) const;

  RoI &getRoI() { return roi; }
  const RoI &getRoI() const { return roi; }

  void addToLPS(Instruction *nI, Instruction *gravity);
  void replaceInLPS(Instruction *nI, Instruction *oI);

  void getExecutingStages(Instruction *inst, std::vector<unsigned> &stages);
  bool ifI2IsInI1IsIn(Instruction *i1, Instruction *i2);

  void assert_strategies_consistent_with_ir();

  std::unordered_set<const TerminatorInst *>
  getSelectedCtrlSpecDeps(const BasicBlock *loopHeader) {
    return selectedCtrlSpecDeps;
  }

  InstInsertPt getInitFcn() { return initFcn; }
  InstInsertPt getFiniFcn() { return finiFcn; }

private:
  typedef std::set<const Value *> VSet;

  RoI roi;
  Module *mod;
  Recovery recovery;
  Type *voidty, *voidptr;
  IntegerType *u8, *u16, *u32, *u64;
  FunctionType *fv2v;
  InstInsertPt initFcn, finiFcn;
  std::vector<Loop *> loops;
  std::unordered_set<const TerminatorInst *> selectedCtrlSpecDeps;

  void init(ModuleLoops &mloops);

  bool fixStaticContexts();
  bool demoteLiveOutsAndPhis(Loop *loop, LiveoutStructure &liveouts);

  bool addInitializationFunction();
  bool addFinalizationFunction();
};

} // namespace SpecPriv
} // namespace liberty

#endif
