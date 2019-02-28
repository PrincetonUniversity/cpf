#define DEBUG_TYPE "selector"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/SLAMP/SLAMPLoad.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
//#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/SmtxSlampManager.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/ModuleLoops.h"


//#include "PtrResidueAA.h"
//#include "LocalityAA.h"
//#include "RoI.h"
#include "liberty/Speculation/RemedSelector.h"
//#include "liberty/Speculation/UpdateOnCloneAdaptors.h"
#include "liberty/Speculation/HeaderPhiPredictionSpeculation.h"


namespace liberty
{
namespace SpecPriv
{

void RemedSelector::getAnalysisUsage(AnalysisUsage &au) const
{
  Selector::analysisUsage(au);

  au.addRequired< SLAMPLoadProfile >();
  au.addRequired< SmtxSlampSpeculationManager >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< ProfileGuidedPredictionSpeculator >();
  au.addRequired< HeaderPhiPredictionSpeculation >();
}

bool RemedSelector::runOnModule(Module &mod)
{
  DEBUG_WITH_TYPE("classify",
    errs() << "#################################################\n"
           << "Remed Selection\n\n\n");

  Vertices vertices;
  Edges edges;
  VertexWeights weights;
  VertexSet maxClique;

  doSelection(vertices, edges, weights, maxClique);

  // sot: remove separation speculation for now
  /*
  // Combine all of these assignments into one big assignment
  Classify &classify = getAnalysis< Classify >();
  for(VertexSet::iterator i=maxClique.begin(), e=maxClique.end(); i!=e; ++i)
  {
    const unsigned v = *i;
    Loop *l = vertices[ v ];
    const HeapAssignment &asgn = classify.getAssignmentFor(l);
    assert( asgn.isValidFor(l) );

    assignment = assignment & asgn;
  }

  DEBUG_WITH_TYPE("classify", errs() << assignment );
  */
  return false;
}

void RemedSelector::resetAfterInline(
  Instruction *callsite_no_longer_exists,
  Function *caller,
  Function *callee,
  const ValueToValueMapTy &vmap,
  const CallsPromotedToInvoke &call2invoke)
{
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  SmtxSlampSpeculationManager &smtxMan = getAnalysis< SmtxSlampSpeculationManager >();

  ctrlspec->reset();
  smtxMan.reset();
}

void RemedSelector::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - Selector::contextRenamedViaClone: " << *changedContext << '\n';
  //assignment.contextRenamedViaClone(changedContext,vmap,cmap,amap);
  Selector::contextRenamedViaClone(changedContext,vmap,cmap,amap);
}


char RemedSelector::ID = 0;
static RegisterPass< RemedSelector > rp("remed-selector", "Remediator Selector");
static RegisterAnalysisGroup< Selector > link(rp);

}
}
