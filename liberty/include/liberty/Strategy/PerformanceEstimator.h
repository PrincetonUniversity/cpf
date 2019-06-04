// A Speculative PS-DSWP transform.
#ifndef LLVM_LIBERTY_SPEC_PRIV_PERFORMANCE_ESTIMATOR_H
#define LLVM_LIBERTY_SPEC_PRIV_PERFORMANCE_ESTIMATOR_H

#include "liberty/Strategy/PipelineStrategy.h"
#include "liberty/Utilities/MakePtr.h"
#include "llvm/Support/Format.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct PerformanceEstimator
{
  virtual ~PerformanceEstimator() {}

  // Give a numeric value which represents the total time
  // spent executing this instruction.  Units are unimportant
  // so long as they are consistent.  Bigger means heavier.
  virtual double estimate_weight(const Instruction *inst) = 0;

  // Give a numeric value which represents the benefit of parallelization of given instruction.
  // Computed by dividing the # of iterations that execute the instruction with the total iteration
  // count of the loop
  virtual double estimate_parallelization_weight(const Instruction *inst, const Loop* loop) = 0;

  // Estimate the weight of some collection of instructions
  template <class InstIter>
  double estimate_weight(const InstIter &begin, const InstIter &end)
  {
    errs() << "Estimated Weight Distribution\n";
    double sum = 0;
    double wt = 0;
    for(InstIter i=begin; i!=end; ++i){
      wt = estimate_weight( MakePointer(*i) );
      sum += wt;
      errs() << format("%.2f", wt) << "\t|\t" << *MakePointer(*i) << "\n";
    }
    return sum;
  }

  // Estimate the parallelization weight of some collection of instructions
  template <class InstIter>
  double estimate_parallelization_weight(const InstIter &begin, const InstIter &end, const Loop *loop)
  {
    double   sum = 0;
    unsigned count = 0;
    for(InstIter i=begin; i!=end; ++i, ++count)
      sum += estimate_parallelization_weight( MakePointer(*i), loop );
    return sum / count;
  }

  // Estimate the weight of a loop
  unsigned long estimate_loop_weight(const Loop *loop)
  {
    unsigned long sum = 0;
    for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    {
      const BasicBlock *bb = *i;
      sum += estimate_weight(bb->begin(), bb->end());
    }
    return sum;
  }

  // Give a numeric value which represents the total running
  // time of a pipeline.  Units are unimportant so long as
  // they are consistent.  Bigger is bad.
  // By default, this method uses the simple pipeline model,
  // which ignores pipeline fill, communication costs, etc:
  //  weight(pipeline) = max_{s in stages} weight(s)
  virtual double estimate_pipeline_weight(const PipelineStrategy::Stages &stages);
  virtual double estimate_pipeline_weight(const PipelineStrategy::Stages &stages, const Loop *loop);
  double estimate_pipeline_weight(const PipelineStrategy &strategy)
    { return estimate_pipeline_weight( strategy.stages ); }
  double estimate_pipeline_weight(const PipelineStrategy &strategy, const Loop *loop)
    { return estimate_pipeline_weight( strategy.stages, loop ); }

};

/// This is the 'dumb' implementation of a performance estimator.
struct FlatPerformanceEstimator : public PerformanceEstimator
{
  /// Each instruction is worth 1.
  /// No matter what kind of instruction.
  /// No matter how much it executes.
  virtual double estimate_weight(const Instruction *inst) { return 1ul; }
  virtual double estimate_parallelization_weight(const Instruction *inst, const Loop* loop)
  {
    return 1.0;
  }
};

}
}

#endif
