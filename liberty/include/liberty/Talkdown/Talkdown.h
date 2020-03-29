#pragma once

#include "SystemHeaders.hpp"
#include "liberty/Talkdown/Node.h"
#include "liberty/Talkdown/Tree.h"

#include <map>
#include <string>

using namespace llvm;

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

      SESENode *getInnermostRegion(Instruction *);
      SESENode *getParent(SESENode *);
      SESENode *getInnermostCommonAncestor(SESENode *, SESENode *);
      std::map<std::string, std::string> &getMetadata(SESENode *); // common metadata

    private:
      bool enabled;

      std::vector<FunctionTree> function_trees;
  };
} // namespace llvm
