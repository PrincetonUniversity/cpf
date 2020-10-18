#ifndef LLVM_LIBERTY_LOOP_PROF_TARGETS_H
#define LLVM_LIBERTY_LOOP_PROF_TARGETS_H

#include "llvm/Pass.h"
//#include "scaf/MemoryAnalysisModules/ProfileInfo.h" // deprecated
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"

#include "liberty/LoopProf/LoopProfLoad.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/Utilities/PrintDebugInfo.h"

#include <vector>
#include <map>

namespace liberty
{
using namespace llvm;

struct header_to_loop_mapping_iterator
{
  typedef std::vector<BasicBlock*>::const_iterator header_iterator;

  header_to_loop_mapping_iterator(const header_iterator &I, ModuleLoops &ml)
    : i(I), mloops(ml) {}

  Loop *operator*() const;
  bool operator==(const header_to_loop_mapping_iterator &other) const;
  bool operator!=(const header_to_loop_mapping_iterator &other) const;

  const header_to_loop_mapping_iterator &operator++();

private:
  header_iterator i;
  ModuleLoops &mloops;
};


struct Targets : public ModulePass
{
  static char ID;
  Targets() : ModulePass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &au) const
  {
    //au.addRequired< ProfileInfo >(); // ProfileInfo is deprecated!
    au.addRequired< BlockFrequencyInfoWrapperPass >();
    au.addRequired< BranchProbabilityInfoWrapperPass >();
    au.addRequired< LoopProfLoad >();
    au.addRequired< ModuleLoops >();

    au.setPreservesAll();
  }

  virtual bool runOnModule(Module &mod);

  typedef std::vector<BasicBlock *> LoopList;
  typedef LoopList::const_iterator header_iterator;

  typedef header_to_loop_mapping_iterator iterator;

  header_iterator begin() const { return Loops.begin(); }
  header_iterator end() const { return Loops.end(); }

  //iterator begin_mloops() const { return iterator(Loops.begin(),mloops); }
  iterator begin(ModuleLoops &mloops) const { return iterator(Loops.begin(),mloops); }
  //iterator end_mloops() const { return iterator(Loops.end(),mloops); }
  iterator end(ModuleLoops &mloops) const { return iterator(Loops.end(),mloops); }

private:
  void addLoopByName(Module &, const std::string &, const std::string &, unsigned long wt, bool minIterCheck = false);
  bool expectsManyIterations(const Loop *loop) ;
  //bool expectsManyIterations(const Loop *loop, const std::string&, const std::string &) ;

  //sot
  ModuleLoops *mloops;

  LoopList Loops;
};

}

#endif


