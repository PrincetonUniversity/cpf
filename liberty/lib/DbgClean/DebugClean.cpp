#define DEBUG_TYPE "debugclean"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"

#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <unordered_set>

namespace liberty {
using namespace llvm;

struct DebugClean : public ModulePass {
  static char ID;
  DebugClean() : ModulePass(ID) {}
  void getAnalysisUsage(AnalysisUsage &au) const {}
  bool runOnModule(Module &mod);
};

bool DebugClean::runOnModule(Module &mod) {
  bool modified;
  std::unordered_set<CallInst *> dbgFunCalls;

  for (Function &F : mod)
    for (BasicBlock &B : F)
      for (Instruction &I : B)
        if (auto *callInst = dyn_cast<CallInst>(&I))
          if (isa<DbgInfoIntrinsic>(callInst))
            dbgFunCalls.insert(callInst);

  modified = !dbgFunCalls.empty();

  for (CallInst *cI : dbgFunCalls)
    cI->eraseFromParent();

  return modified;
}

char DebugClean::ID = 0;
static RegisterPass<DebugClean> mpp("dbg-clean",
                                    "Clean up debug function calls");

} // namespace liberty
