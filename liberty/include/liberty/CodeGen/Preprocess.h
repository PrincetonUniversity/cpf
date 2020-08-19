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
#include "liberty/Redux/Reduction.h"

#include "PDG.hpp"

#include <set>
#include <unordered_set>
#include <unordered_map>

namespace liberty {
namespace SpecPriv {
using namespace llvm;

struct Preprocess : public ModulePass {
  static char ID;
  Preprocess() : ModulePass(ID), normalCount(0), localCount(0), killCount(0),
                 predCount(0), privateerCount(0), sharedCount(0) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

  Recovery &getRecovery() { return recovery; }
  const RecoveryFunction &getRecoveryFunction(Loop *loop) const;

  RoI &getRoI() { return roi; }
  const RoI &getRoI() const { return roi; }

  void addToLPS(Instruction *nI, Instruction *gravity,
                bool forceReplication = false);
  void replaceInLPS(Instruction *nI, Instruction *oI);

  void getExecutingStages(Instruction *inst, std::vector<unsigned> &stages);
  bool ifI2IsInI1IsIn(Instruction *i1, Instruction *i2);

  void assert_strategies_consistent_with_ir();

  void replaceLiveOutUsage(Instruction *def, unsigned i, Loop *loop,
                           StringRef name, Instruction *object, bool redux);

  std::unordered_set<const PHINode *> getIndVarPhis () const{
    return indVarPhis;
  }

  std::unordered_set<const Instruction *> *
  getSelectedCtrlSpecDeps(const BasicBlock *loopHeader) {
    if (selectedCtrlSpecDeps.count(loopHeader))
      return &selectedCtrlSpecDeps[loopHeader];
    else
      return nullptr;
  }

  std::unordered_map<const BasicBlock *, std::unordered_set<const LoadInst *>> *
  getSelectedPrivateSpecLoads() {
    return &selectedPrivateSpecLoads;
  }

  std::unordered_set<const Value *> *
  getSelectedLoadedValuePreds(const BasicBlock *loopHeader) {
    if (selectedLoadedValuePreds.count(loopHeader))
      return &selectedLoadedValuePreds[loopHeader];
    else
      return nullptr;
  }

  bool isSeparationSpecUsed(BasicBlock *loopHeader) {
    return separationSpecUsed.count(loopHeader);
  }

  bool getChunking () const {return Chunking;}

  bool isSpecUsed(BasicBlock *loopHeader) const {
    return specUsed.count(loopHeader);
  }

  bool isCheckpointingNeeded(BasicBlock *loopHeader) const {
    return checkpointNeeded.count(loopHeader);
  }

  InstInsertPt getInitFcn() const {
    return initFcn;
  }
  InstInsertPt getFiniFcn() const { return finiFcn; }

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
  std::unordered_map<const BasicBlock *, std::unordered_set<const Instruction *>>
      selectedCtrlSpecDeps;
  std::unordered_map<const BasicBlock *, std::unordered_set<const LoadInst *>>
      selectedPrivateSpecLoads;
  std::unordered_map<const BasicBlock *, std::unordered_set<const Value *>>
      selectedLoadedValuePreds;

  std::unordered_set<const BasicBlock *> separationSpecUsed;
  std::unordered_set<const BasicBlock *> specUsed;
  std::unordered_set<const BasicBlock *> checkpointNeeded;

  std::unordered_set<const Instruction *> reduxV;
  std::unordered_map<const Instruction *, Reduction::ReduxInfo> redux2Info;
  std::unordered_map<const BasicBlock *, const Instruction *> reduxUpdateInst;
  std::unordered_set<const PHINode *> indVarPhis;
  //const PHINode *indVarPhi;
  bool Chunking;

  // collect all the AUs that appeared in various types of selected
  // privitization remedies
  HeapAssignment::AUSet normalPrivAUs;
  //HeapAssignment::AUSet localPrivAUs;
  HeapAssignment::AUSet killPrivAUs;
  HeapAssignment::AUSet predPrivAUs;
  HeapAssignment::AUSet privateerPrivAUs;

  uint64_t normalCount;
  uint64_t localCount;
  uint64_t killCount;
  uint64_t predCount;
  uint64_t privateerCount;
  uint64_t sharedCount;

  void init(ModuleLoops &mloops);

  bool fixStaticContexts();
  bool demoteLiveOutsAndPhis(Loop *loop, LiveoutStructure &liveouts,
                             ModuleLoops &mloops);

  void moveStackLocals(HeapAssignment &asgn, const Loop *L);
  void moveLocalPrivs(HeapAssignment &asgn);
  void moveKillPrivs(HeapAssignment &asgn);
  uint64_t collectRelevantAUs(const Value *ptr, const Read &spresults,
                          Ctx *loop_ctx, HeapAssignment::AUSet &relAUs);

  bool addInitializationFunction();
  bool addFinalizationFunction();
};

} // namespace SpecPriv
} // namespace liberty

#endif
