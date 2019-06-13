#ifndef LLVM_LIBERTY_SPEC_PRIV_PROFILE_PERFORMANCE_ESTIMATOR_H
#define LLVM_LIBERTY_SPEC_PRIV_PROFILE_PERFORMANCE_ESTIMATOR_H

#include "liberty/Strategy/PerformanceEstimator.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct ProfilePerformanceEstimator : public ModulePass, public PerformanceEstimator
{
  static char ID;
  ProfilePerformanceEstimator() : ModulePass(ID) {}

  StringRef getPassName() const { return "Profile-guided Performance Estimator"; }

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

  virtual double estimate_weight(const Instruction *inst);

  virtual double estimate_parallelization_weight(const Instruction *inst, const Loop* target_loop);

  void reset();

  // Return a relative weight, which is based on
  // the execution frequency of this instruction (from edge count profile)
  // and instruction latency (i.e. loads/stores more expensive than add/sub)
  unsigned long relative_weight(const Instruction *inst);

  static unsigned instruction_type_weight(const Instruction *inst);

private:
  // Root case
  void visit(const Function *fcn);

  // Recursive case
  template <class SubLoopIter, class MemberIter>
  void visit(
    const Function *fcn, const Loop *loop,
    const SubLoopIter &subloop_begin, const SubLoopIter &subloop_end,
    const MemberIter &member_begin, const MemberIter &member_end
    );

  // Represents a context for which we have execution time
  // data.  <F,0> represents a while function; <F,l> represents
  // a loop l in F.
  typedef std::pair<const Function *, const Loop *> Context;

  // A pair of total execution time of 'local' instructions
  // and the sum of the relative weight of 'local' instructions.
  typedef std::pair<unsigned long, unsigned long> TimeAndWeight;

  // Information about each context.
  typedef std::map<Context,TimeAndWeight> Context2TimeAndWeight;

  Context2TimeAndWeight ctx2timeAndWeight;
};

}
}

#endif

