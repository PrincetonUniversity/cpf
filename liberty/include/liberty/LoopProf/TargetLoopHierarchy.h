#ifndef LLVM_LIBERTY_TARGET_LOOP_HIERARCHY
#define LLVM_LIBERTY_TARGET_LOOP_HIERARCHY

#include "liberty/LoopProf/Targets.h"
#include "scaf/Utilities/ModuleLoops.h"

namespace liberty
{

using namespace llvm;

struct TargetLoopHierarchy : public ModulePass
{
  static char ID;
  TargetLoopHierarchy() : ModulePass(ID) {}
  ~TargetLoopHierarchy() {}

  void getAnalysisUsage(AnalysisUsage& au) const
  {
    au.addRequired< ModuleLoops >();
    au.addRequired< Targets >();
//    au.addRequired< LoopInfo >();
    au.setPreservesAll();
  }

  bool runOnModule(Module& m);
  bool hasHotSubloop(Loop* l, Targets& targets);
};

}

#endif
