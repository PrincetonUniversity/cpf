#pragma once

#include "llvm/Pass.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Speculation/Classify.h"

#include "liberty/Talkdown/Talkdown.h"

namespace liberty
{
// Note that this is different from the global ::AutoMP namespace that the talkdown
// module is contained under
namespace AutoMP
{
using namespace llvm;
using namespace SpecPriv;

// You can use it as a LoopAA too!
struct TalkdownAA : public ModulePass, public LoopAA // Not a pass!
{
  TalkdownAA() : ModulePass(ID), LoopAA() {}
  virtual SchedulingPreference getSchedulingPreference() const
  {
    return SchedulingPreference(Normal + 10); // XXX No idea if this is the right preference...
  }

  StringRef getLoopAAName() const { return "talkdown-aa"; }

  static char ID;

  bool runOnModule(Module &mod);
  void getAnalysisUsage(AnalysisUsage &AU) const;

  // This is so tricky! Should be documented somewhere in LoopAA maybe...
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA*)this;
    return this;
  }

  LoopAA::AliasResult alias(const Value *P1, unsigned S1, TemporalRelation rel,
                            const Value *P2, unsigned S2, const Loop *L,
                            Remedies &R,
                            DesiredAliasResult dAliasRes = DNoOrMustAlias);

  void setLoopOfInterest(Loop *l) { loop = l; }

private:
  Loop *loop;
};

} // namespace AutoMP
} // namespace liberty
