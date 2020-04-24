#pragma once

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"

#include "liberty/Talkdown/Node.h"
#include "liberty/Talkdown/Tree.h"

#include <map>
#include <string>

using namespace llvm;
using namespace AutoMP;

namespace llvm
{
  struct Talkdown : public ModulePass
  {
    public:
      static char ID;

      Talkdown() : ModulePass( ID ) {}

      bool doInitialization(Module &M);
      bool runOnModule(Module &M);
      void getAnalysisUsage(AnalysisUsage &AU) const;

      /* SESENode *getInnermostRegion(Instruction *); */
      /* SESENode *getParent(SESENode *); */
      /* SESENode *getInnermostCommonAncestor(SESENode *, SESENode *); */
      /* std::map<std::string, std::string> &getMetadata(SESENode *); // common metadata */
      bool areIndependent(Instruction *i1, Instruction *i2);

    private:
      bool enabled;

      std::vector<FunctionTree> function_trees;
  };
} // namespace llvm
