// Modifies the code before parallelization.
// - Add validation for pointer-residue speculation
#ifndef LLVM_LIBERTY_SPECPRIV_APPLY_SMTX_SPEC_H
#define LLVM_LIBERTY_SPECPRIV_APPLY_SMTX_SPEC_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/InstInsertPt.h"

#include "Api.h"
#include "Classify.h"
#include "Recovery.h"
#include "RoI.h"

#include <set>

#define DEBUG_MEMORY_FOOTPRINT 1
#define DEBUG_BASICBLOCK_TRACE 1

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


struct ApplySmtxSpec : public ModulePass
{
  static char ID;
  ApplySmtxSpec() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  typedef std::set<const Value*> VSet;

  Module *mod;
  Type *voidty, *voidptr;
  IntegerType *u16;
  std::vector<Loop*> loops;

  void init(ModuleLoops &mloops);

  bool runOnLoop(Loop *loop, std::vector<Instruction*> &all_mem_ops);
  bool addSmtxChecks(Loop *loop, std::vector<Instruction*> &all_mem_ops);
  bool addSmtxMemallocs(Loop *loop, std::vector<Instruction*> &all_memalloc_ops);

  Api *api;
};

}
}


#endif

