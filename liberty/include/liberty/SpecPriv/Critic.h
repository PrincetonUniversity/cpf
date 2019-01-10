#ifndef LLVM_LIBERTY_CRITIC_H
#define LLVM_LIBERTY_CRITIC_H

#include "liberty/SpecPriv/PDG.h"
#include "liberty/SpecPriv/DAGSCC.h"
#include "liberty/SpecPriv/PipelineStrategy.h"
#include "liberty/SpecPriv/PerformanceEstimator.h"
#include "liberty/SpecPriv/Pipeline.h"
#include "liberty/SpecPriv/Selector.h"

#include <set>
#include <memory>
#include <tuple>

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

// Criticism is a PDG edge with a boolean value to differentiate loop-carried
// from intra-iteration edges. Also specify type of dep (mem/reg/ctrl)
typedef std::tuple<Vertices::ID, Vertices::ID, bool, DepType> Criticism;

typedef std::set<Criticism> Criticisms;

typedef PipelineStrategy ParallelizationPlan;

struct CriticRes {
  Criticisms criticisms;
  long expSpeedup;
  std::unique_ptr<ParallelizationPlan> ps;
};

class Critic {
public:
  Critic(PerformanceEstimator *p, unsigned tb, LoopProfLoad *l)
      : perf(p), threadBudget(tb), lpl(l) {}
  static Criticisms getAllCriticisms(const PDG &pdg);
  static void printCriticisms(raw_ostream &fout, Criticisms &criticisms,
                              const PDG &pdg);
  void complainAboutEdge(CriticRes &res, const PDG &pdg, Vertices::ID src,
                         Vertices::ID dst, bool loopCarried);
  std::unique_ptr<ParallelizationPlan> getPipelineStrategy(PDG &pdg);
  long getExpPipelineSpeedup(const ParallelizationPlan &ps, const PDG &pdg);

  virtual CriticRes getCriticisms(const PDG &pdg) = 0;
  virtual StringRef getCriticName() const = 0;

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
  CriticRes getCriticisms(const PDG &pdg);
  StringRef getCriticName() const {return "doall-critic";};
};

} // namespace liberty

#endif
