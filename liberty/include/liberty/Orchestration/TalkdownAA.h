#pragma once

#include "liberty/Analysis/ClassicLoopAA.h"

#include "liberty/Talkdown/Talkdown.h"

namespace liberty
{
// Note that this is different from the global ::AutoMP namespace that the talkdown
// module is contained under
namespace AutoMP
{
// You can use it as a LoopAA too!
struct TalkdownAA : public ClassicLoopAA // Not a pass!
{
  TalkdownAA() : ClassicLoopAA() {}
  virtual SchedulingPreference getSchedulingPreference() const
  {
    return SchedulingPreference(Top); // XXX No idea if this is the right preference...
  }

  StringRef getLoopAAName() const { return "talkdown-aa"; }

  void setLoopOfInterest(Loop *l) { loop = l; }

private:
  Loop *loop;
};

} // namespace AutoMP
} // namespace liberty
