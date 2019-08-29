#define DEBUG_TYPE "pipeline"

#include "PerformanceEstimator.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

unsigned long PerformanceEstimatorOLD::estimate_pipeline_weight(const PipelineStrategyOLD::Stages &stages)
{
  unsigned long max = 0;
  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    const PipelineStageOLD &stage = stages[i];
    assert( stage.type != PipelineStageOLD::Replicable && "Must expand replicated stages before estimation");

    unsigned long wt = estimate_weight( stage.instructions.begin(), stage.instructions.end() );

    const unsigned rep = stage.parallel_factor;
    if( rep > 1 )
      wt = (wt + rep - 1) / rep;

    const unsigned long rep_wt = estimate_weight( stage.replicated.begin(), stage.replicated.end() );
    wt += rep_wt;

    if( wt > max )
      max = wt;
  }
  return max;
}

unsigned long PerformanceEstimatorOLD::estimate_pipeline_weight(const PipelineStrategyOLD::Stages &stages, const Loop *loop)
{
  errs() << "\t*** estimate pipeline weights\n";

  unsigned long max = 0;
  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    const PipelineStageOLD &stage = stages[i];
    assert( stage.type != PipelineStageOLD::Replicable && "Must expand replicated stages before estimation");

    unsigned long wt = estimate_weight( stage.instructions.begin(), stage.instructions.end() );
    unsigned long seq_wt = wt;

    unsigned rep = stage.parallel_factor;
    if( rep > 1 )
    {
      double par_wt = estimate_parallelization_weight( stage.instructions.begin(), stage.instructions.end(), loop );

      rep = (double)rep * par_wt;

      if (rep == 0) rep = 1;

      // errs() << "original wt " << wt << " p-weight " << par_wt << " rep " << rep << "\n";

      wt = (wt + rep - 1) / rep;
    }

    const unsigned long rep_wt = estimate_weight( stage.replicated.begin(), stage.replicated.end() );

    wt += rep_wt;
    seq_wt += rep_wt;

    double speedup = wt ? (seq_wt / wt) : 0;

    errs() << "\t\t- Stage " << format("%2d", i)
           << " Weight " << format("%6.2f", 100.0 * (double)seq_wt / estimate_loop_weight(loop))
           << " P-Factor " << format("%4d", rep)
           << " Speedup " << format("%6.2f", speedup)
           << " Absolute P-Weight " << wt
           << "\n";

    if( wt > max )
      max = wt;
  }

  errs() << "\t\t- Weight " << max << "\n";
  return max;
}


}
}

