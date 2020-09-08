#ifndef LLVM_LIBERTY_CRITIC_H
#define LLVM_LIBERTY_CRITIC_H

#include "liberty/Strategy/PerformanceEstimator.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/Utilities/PrintDebugInfo.h"
#include "PDG.hpp"
#include "SCC.hpp"
#include "SCCDAG.hpp"

#include "Assumptions.h"
#include "LoopDependenceInfo.hpp"

#include <set>
#include <memory>
#include <tuple>
#include <vector>

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

typedef PipelineStrategy ParallelizationPlan;

struct CriticRes {
  Criticisms criticisms;
  unsigned long expSpeedup;
  std::unique_ptr<ParallelizationPlan> ps;
};

class Critic {
public:
  Critic(PerformanceEstimator *p, unsigned tb, LoopProfLoad *l)
      : perf(p), threadBudget(tb), lpl(l) {}
  static Criticisms getAllCriticisms(const PDG &pdg);
  static void printCriticisms(raw_ostream &fout, Criticisms &criticisms,
                              const PDG &pdg);
  unsigned long getExpPipelineSpeedup(const ParallelizationPlan &ps,
                                      const PDG &pdg, Loop *loop);

  virtual CriticRes getCriticisms(PDG &pdg, Loop *loop) = 0;
  virtual StringRef getCriticName() const = 0;

  static const unsigned FixedPoint;
  static const unsigned PenalizeLoopNest;

protected:
  PerformanceEstimator *perf;
  unsigned threadBudget;
  LoopProfLoad *lpl;
};

class DOALLCritic : public Critic {
public:
  DOALLCritic(PerformanceEstimator *perf, unsigned threadBudget,
              LoopProfLoad *lpl)
      : Critic(perf, threadBudget, lpl) {}
  CriticRes getCriticisms(PDG &pdg, Loop *loop);
  std::unique_ptr<ParallelizationPlan> getDOALLStrategy(PDG &pdg, Loop *loop);
  StringRef getCriticName() const {return "doall-critic";};
};

} // namespace liberty

#endif
