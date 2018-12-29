#define DEBUG_TYPE "moduleloops"

#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Utilities/ModuleLoops.h"

namespace liberty
{
using namespace llvm;

GimmeLoops &ModuleLoops::compute(const Function *fcn)
{
  if( !results.count(fcn) )
  {
    // Evil, but okay because NONE of these passes modify the IR
    Function *non_const_function = const_cast<Function*>(fcn);

    //errs() << "Computing loops for " << fcn->getName() << '\n';

    results[fcn] = new GimmeLoops();
    results[fcn]->init(td, tli, non_const_function, true);
  }

  return *results[fcn];
}

DominatorTree &ModuleLoops::getAnalysis_DominatorTree(const Function *fcn)
{
  GimmeLoops &gl = compute(fcn);
  return * gl.getDT();
}

PostDominatorTree &ModuleLoops::getAnalysis_PostDominatorTree(const Function *fcn)
{
  GimmeLoops &gl = compute(fcn);
  return * gl.getPDT();
}

LoopInfo &ModuleLoops::getAnalysis_LoopInfo(const Function *fcn)
{
  GimmeLoops &gl = compute(fcn);
  return *gl.getLI();
}

ScalarEvolution &ModuleLoops::getAnalysis_ScalarEvolution(const Function *fcn)
{
  GimmeLoops &gl = compute(fcn);
  return * gl.getSE();
}

char ModuleLoops::ID = 0;
static RegisterPass< ModuleLoops > rp("mloops", "ModuleLoops: get your pass manager on...");

}
