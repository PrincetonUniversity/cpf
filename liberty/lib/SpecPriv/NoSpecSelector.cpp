#define DEBUG_TYPE "selector"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/SpecPriv/Pipeline.h"
#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/ProfilePerformanceEstimator.h"
#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/Utilities/ModuleLoops.h"

#include "RoI.h"
#include "NoSpecSelector.h"
//#include "Transform.h"


namespace liberty
{
namespace SpecPriv
{
void NoSpecSelector::getAnalysisUsage(AnalysisUsage &au) const
{
  Selector::analysisUsage(au);
}

bool NoSpecSelector::runOnModule(Module &mod)
{
  DEBUG_WITH_TYPE("classify",
    errs() << "#################################################\n"
           << " No-spec Selection\n\n\n");

  Vertices vertices;
  Edges edges;
  VertexWeights weights;
  VertexSet maxClique;

  doSelection(vertices, edges, weights, maxClique);

  return false;
}

char NoSpecSelector::ID = 0;
static RegisterPass< NoSpecSelector > rp("no-spec-selector", "No-Speculation Selector");
static RegisterAnalysisGroup< Selector > link(rp);

}
}
