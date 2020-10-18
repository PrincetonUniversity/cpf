#pragma once

#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "scaf/Utilities/ControlSpecIterators.h"
#include "scaf/Utilities/ControlSpeculation.h"
#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/MemoryAnalysisModules/SimpleAA.h"
#include "liberty/Orchestration/EdgeCountOracleAA.h"
#include "liberty/Orchestration/PointsToAA.h"
#include "liberty/Orchestration/PredictionSpeculation.h"
#include "liberty/Orchestration/PtrResidueAA.h"
#include "liberty/Orchestration/ReadOnlyAA.h"
#include "liberty/Orchestration/ShortLivedAA.h"
#include "liberty/Orchestration/SmtxAA.h"
#include "liberty/Speculation/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
#include "scaf/Utilities/LoopDominators.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"

#include "PDG.hpp"

#include <unordered_set>
#include <unordered_map>

using namespace llvm;
using namespace liberty;

namespace llvm {
struct PDGBuilder : public ModulePass {
public:
  static char ID;
  PDGBuilder() : ModulePass(ID) {}
  virtual ~PDGBuilder() {}

  // bool doInitialization (Module &M) override ;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;

  std::unique_ptr<PDG> getLoopPDG(Loop *loop);

private:
  const DataLayout *DL;
  NoControlSpeculation noctrlspec;
  SmtxAA *smtxaa;
  EdgeCountOracle *edgeaa;
  PredictionAA *predaa;
  ControlSpeculation *ctrlspec;
  PredictionSpeculation *predspec;
  PtrResidueAA *ptrresaa;
  PointsToAA *pointstoaa;
  ReadOnlyAA *roaa;
  ShortLivedAA *localaa;
  SimpleAA *simpleaa;
  Read *spresults;
  Classify *classify;
  KillFlow_CtrlSpecAware *killflow_aware;
  CallsiteDepthCombinator_CtrlSpecAware *callsite_aware;

  void addSpecModulesToLoopAA();
  void specModulesLoopSetup(Loop *loop);
  void removeSpecModulesFromLoopAA();
  void constructEdgesFromUseDefs(PDG &pdg, Loop *loop);
  void constructEdgesFromMemory(PDG &pdg, Loop *loop, LoopAA *aa);
  void constructEdgesFromControl(PDG &pdg, Loop *loop);

  void queryMemoryDep(Instruction *src, Instruction *dst,
                      LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV,
                      Loop *loop, LoopAA *aa, PDG &pdg);

  void queryLoopCarriedMemoryDep(Instruction *src, Instruction *dst, Loop *loop,
                                 LoopAA *aa, PDG &pdg);

  void queryIntraIterationMemoryDep(Instruction *src, Instruction *dst,
                                    Loop *loop, LoopAA *aa, PDG &pdg);

  void annotateMemDepsWithRemedies(PDG &pdg, Loop *loop, LoopAA *aa);
};
} // namespace llvm
