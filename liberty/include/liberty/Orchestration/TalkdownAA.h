#pragma once

#include "llvm/Pass.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Speculation/Classify.h"

#include "liberty/Talkdown/Talkdown.h"

namespace liberty
{
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

    LoopAA::ModRefResult modref(const Instruction *A,
                              TemporalRelation rel,
                              const Instruction *B,
                              const Loop *L,
                              Remedies &R);

    void setLoopOfInterest(Loop *l) { loop = l; }

  private:
    Loop *loop;
    Talkdown *talkdown;
  };

} // namespace liberty
