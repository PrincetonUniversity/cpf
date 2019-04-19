#ifndef LLVM_LIBERTY_CRITIC_H
#define LLVM_LIBERTY_CRITIC_H

#include "llvm/IR/DebugInfoMetadata.h"

#include "liberty/Strategy/PerformanceEstimator.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "PDG.hpp"
#include "SCC.hpp"
#include "SCCDAG.hpp"

#include "LoopDependenceInfo.hpp"

#include <set>
#include <memory>
#include <tuple>
#include <vector>

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

// Criticism is a PDG edge with a boolean value to differentiate loop-carried
// from intra-iteration edges. Also specify type of dep (mem/reg/ctrl)
typedef DGEdge<Value> Criticism;

typedef std::set<Criticism *> Criticisms;

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

  virtual CriticRes getCriticisms(PDG &pdg, Loop *loop,
                                  LoopDependenceInfo &ldi) = 0;
  virtual StringRef getCriticName() const = 0;

  static const unsigned FixedPoint;
  static const unsigned PenalizeLoopNest;

  static void printInstDebugInfo(Instruction *I);

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
  CriticRes getCriticisms(PDG &pdg, Loop *loop, LoopDependenceInfo &ldi);
  std::unique_ptr<ParallelizationPlan> getDOALLStrategy(PDG &pdg, Loop *loop);
  StringRef getCriticName() const {return "doall-critic";};
};

} // namespace liberty

#endif
