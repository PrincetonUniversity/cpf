#pragma once

#include "SystemHeaders.hpp"
#include "Node.h"
#include "Tree.h"

#include <map>
#include <string>

using namespace llvm;

namespace llvm
{
  struct TalkdownTester : public ModulePass
  {
    public:
      static char ID;

      TalkdownTester() : ModulePass( ID ) {}

      bool doInitialization(Module &M);
      bool runOnModule(Module &M);
      void getAnalysisUsage(AnalysisUsage &AU) const;
  };
} // namespace llvm
