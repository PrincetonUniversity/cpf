#include "liberty/Strategy/PerformanceEstimator.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

double PerformanceEstimator::estimate_pipeline_weight(const PipelineStrategy::Stages &stages)
{
  double max = 0;
  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    const PipelineStage &stage = stages[i];
    assert( stage.type != PipelineStage::Replicable && "Must expand replicated stages before estimation");

    double wt = estimate_weight( stage.instructions.begin(), stage.instructions.end() );

    const unsigned rep = stage.parallel_factor;
    if( rep > 1 )
      wt = (wt + rep - 1) / rep;

    const double rep_wt = estimate_weight( stage.replicated.begin(), stage.replicated.end() );
    wt += rep_wt;

    if( wt > max )
      max = wt;
  }
  return max;
}

double PerformanceEstimator::estimate_pipeline_weight(const PipelineStrategy::Stages &stages, const Loop *loop)
{
  errs() << "\t*** estimate pipeline weights\n";

  double estimate_loop_wt = estimate_loop_weight(loop);
  double max = 0;
  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    const PipelineStage &stage = stages[i];
    assert( stage.type != PipelineStage::Replicable && "Must expand replicated stages before estimation");

    double wt = estimate_weight( stage.instructions.begin(), stage.instructions.end() );
    double seq_wt = wt;

    unsigned rep = stage.parallel_factor;
    if( rep > 1 )
    {
      double par_wt = estimate_parallelization_weight( stage.instructions.begin(), stage.instructions.end(), loop );

      rep = (double)rep * par_wt;

      if (rep == 0) rep = 1;

      // errs() << "original wt " << wt << " p-weight " << par_wt << " rep " << rep << "\n";

      wt = (wt + rep - 1) / rep;
    }

    const double rep_wt = estimate_weight( stage.replicated.begin(), stage.replicated.end() );

    wt += rep_wt;
    seq_wt += rep_wt;

    double speedup = wt ? (seq_wt / wt) : 0;

    errs() << "\t\t- Stage " << format("%2d", i)
           << " Weight " << format("%6.2f", 100.0 * (double)seq_wt / estimate_loop_wt)
           << " P-Factor " << format("%4d", rep)
           << " Speedup " << format("%6.2f", speedup)
           << " Absolute P-Weight " << format("%6.2f", wt)
           << "\n";

    if( wt > max )
      max = wt;
  }

  errs() << "\t\t- Weight " << format("%6.2f", max) << "\n";
  return max;
}


}
}

