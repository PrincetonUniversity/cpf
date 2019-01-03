// Given a set of hot loops with assignments,
// decide which loops to parallelize, and
// form a compatible assignment.
#ifndef LLVM_LIBERTY_NO_SPEC_SELECTOR_H
#define LLVM_LIBERTY_NO_SPEC_SELECTOR_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"

#include <vector>
#include <set>
#include <map>

#include "liberty/LoopProf/Targets.h"
#include "liberty/SpecPriv/UpdateOnClone.h"
#include "liberty/SpecPriv/PipelineStrategy.h"
#include "liberty/SpecPriv/Selector.h"

#include "Ebk.h"

namespace liberty
{
namespace SpecPriv
{

struct NoSpecSelector : public ModulePass, public Selector
{
  static char ID;
  NoSpecSelector() : ModulePass(ID), Selector() {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

  StringRef getPassName() const { return "nospec-selector"; }

  // Isn't multiple inheritance wonderful!?
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI)
  {
    if(PI == &NoSpecSelector::ID)
      return (NoSpecSelector*)this;
    else if(PI == &Selector::ID)
      return (Selector*)this;
    return this;
  }

protected:
  virtual Pass &getPass() { return *this; }
};


}
}

#endif

