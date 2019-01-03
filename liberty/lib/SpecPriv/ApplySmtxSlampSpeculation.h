// Modifies the code before parallelization.
// - Add validation for pointer-residue speculation
#ifndef LLVM_LIBERTY_SPECPRIV_APPLY_SMTX_SLAMP_SPEC_H
#define LLVM_LIBERTY_SPECPRIV_APPLY_SMTX_SLAMP_SPEC_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/InstInsertPt.h"

#include "Api.h"
#include "Classify.h"
#include "Recovery.h"
#include "RoI.h"

#include <set>
#include <map>

#define DEBUG_MEMORY_FOOTPRINT 0
#define DEBUG_BASICBLOCK_TRACE 0

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


struct ApplySmtxSlampSpec : public ModulePass
{
  static char ID;
  ApplySmtxSlampSpec() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &module);

private:
  typedef std::set<const Value*> VSet;

  Module *mod;
  Type *voidty, *voidptr;
  IntegerType *u16;
  std::vector<Loop*> loops;

  typedef std::vector<Instruction*>    InstVec;
  typedef std::vector<LoadInst*>       LoadVec;
  typedef std::vector<StoreInst*>      StoreVec;
  typedef std::map<Function*, InstVec> Fcn2MemOps;

  std::set<Function*> visited;

  void init(ModuleLoops &mloops);

  bool addSmtxMemallocs(InstVec &all_memalloc_ops);
  bool addSeparationMemallocs(Module& m,
      std::map<unsigned, unsigned>& global2versioned,
      std::map<unsigned, unsigned>& global2nonversioned
  );

  bool      isValidSeparationMemallocs(CallInst* ci);
  Constant* getMatchingVersionedSeparationMemalloc(Function* fcn);
  Constant* getMatchingSeparationMemalloc(Function* fcn);

  bool runOnLoop(
      Loop *loop,
      InstVec &all_mem_ops,
      std::set<const Instruction*>& skippables,
      std::map<unsigned, unsigned>& g2v,
      std::map<unsigned, unsigned>& g2nv
      );
  bool addSmtxChecks(Loop *loop, InstVec &all_mem_ops, std::set<const Instruction*>& skippables);
  bool addPredictionChecks(InstInsertPt& pt);
  bool addSeparationSetup(
      Loop* loop,
      InstInsertPt& pt,
      std::map<unsigned, unsigned>& g2v,
      std::map<unsigned, unsigned>& g2nv
      );

  void IntraProceduralOptimization(InstVec& all_mem_ops, std::set<const Instruction*>& skippables);
  void InterProceduralOptimization(set<const Instruction*>& skippables);

  void findMustExecuted(
      RoI::FSet& fcns,
      std::map<const Function*, LoadVec>& must_executed_loads,
      std::map<const Function*, StoreVec>& must_executed_stores,
      std::map<const Function*, InstVec>& must_executed_calls
  );

  void myDominators(Instruction* me, RoI::BBSet& roots,
      std::map<const Function*, std::vector<LoadInst*> >&  must_executed_loads,
      std::map<const Function*, std::vector<StoreInst*> >& must_executed_stores,
      std::map<const Function*, std::vector<Instruction*> >&  must_executed_calls,
      /* output */
      std::set<LoadInst*>&  loads,
      std::set<StoreInst*>& stores
      );

  Function* getLinearPredictor(LoadInst* li, unsigned read_size_primitive);

#if 0
  //std::map<BasicBlock*, std::map<LoadInst*, unsigned> > loop2liloads;
  //std::map<BasicBlock*, std::map<LoadInst*, unsigned> > loop2lploads;
#endif

  std::map<LoadInst*, unsigned> liloads;
  std::map<LoadInst*, unsigned> lploads;

  std::set<LoadInst*> li_instrumented;
  std::set<LoadInst*> lp_instrumented;

  Api *api;
};

}
}


#endif

