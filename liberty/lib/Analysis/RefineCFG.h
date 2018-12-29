#ifndef REFINE_CFG_H
#define REFINE_CFG_H

#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/CallSite.h"

class RefineCFG : public llvm::ModulePass {

private:

  llvm::CallGraph *CG;

  bool runOnFunction(const llvm::Function &F);
  bool runOnCallSite(const llvm::CallSite &CS);

public:
  static char ID;

  RefineCFG() : llvm::ModulePass(ID), CG(NULL) {}

  bool runOnModule(llvm::Module &M);
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const;
};

#endif /* REFINE_CFG_H */
