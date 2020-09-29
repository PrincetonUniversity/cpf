#ifndef LLVM_LIBERTY_LAMP_LOOP_SAMPLE
#define LLVM_LIBERTY_LAMP_LOOP_SAMPLE

#include "llvm/Pass.h"

namespace liberty {
  using namespace llvm;

  class LAMPLoopSample: public ModulePass
  {
    private:
      int funcId;
      std::map<Function *, Function *> funcMap;

    public:
      static char ID;
      LAMPLoopSample();
      ~LAMPLoopSample();

      void reset();

      StringRef getPassName() const { return "LAMPLoopSample"; }

      void *getAdjustedAnalysisPointer(AnalysisID PI) {
        return this;
      }

      bool runOnModule(Module &M);
      void removeLampCalls(std::list<Instruction *> &delList);
      void replaceCall(Module &M, Instruction *inst);
      void replaceCalls(Module &M, std::list<Instruction *> &callList);
      void sanitizeFunctions(Module &M);
      void sanitizeLoop(Loop *loop, Module &M);
      void fixBackEdge(BasicBlock *, BasicBlock *, BasicBlock*);
      Loop* LAMPCloneLoop(Loop *OrigL, LoopInfo *LI, ValueToValueMapTy &clonedValueMap,
          SmallVector<BasicBlock *, 16> &ClonedBlocks);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  };

}
#endif

