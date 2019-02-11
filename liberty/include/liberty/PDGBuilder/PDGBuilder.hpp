#pragma once

#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "PDG.hpp"

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
  NoControlSpeculation noctrlspec;

  void constructEdgesFromUseDefs(PDG &pdg, Loop *loop);
  void constructEdgesFromMemory(PDG &pdg, Loop *loop, LoopAA *aa);
  void constructEdgesFromControl(PDG &pdg, Loop *loop,
                                 PostDominatorTree &postDomTree);

  void queryMemoryDep(Instruction *src, Instruction *dst,
                      LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV,
                      Loop *loop, LoopAA *aa, PDG &pdg);

  void queryLoopCarriedMemoryDep(Instruction *src, Instruction *dst, Loop *loop,
                                 LoopAA *aa, PDG &pdg);

  void queryIntraIterationMemoryDep(Instruction *src, Instruction *dst,
                                    Loop *loop, LoopAA *aa, PDG &pdg);
};
} // namespace llvm
