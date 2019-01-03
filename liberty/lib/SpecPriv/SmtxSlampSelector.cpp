#define DEBUG_TYPE "selector"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/SLAMP/SLAMPLoad.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/Pipeline.h"
#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/ProfilePerformanceEstimator.h"
#include "liberty/Utilities/ModuleLoops.h"

#include "RoI.h"
#include "SmtxAA.h"
#include "SmtxSlampSelector.h"
#include "UpdateOnCloneAdaptors.h"
#include "HeaderPhiPredictionSpeculation.h"


namespace liberty
{
namespace SpecPriv
{

void SmtxSlampSelector::getAnalysisUsage(AnalysisUsage &au) const
{
  Selector::analysisUsage(au);

  au.addRequired< LAMPLoadProfile >();
  au.addRequired< SLAMPLoadProfile >();
  au.addRequired< SmtxSpeculationManager >();
  au.addRequired< SmtxSlampSpeculationManager >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< HeaderPhiPredictionSpeculation >();
}

void SmtxSlampSelector::buildSpeculativeAnalysisStack(const Loop *A)
{
  Module* M = A->getHeader()->getParent()->getParent();
  const DataLayout &DL = M->getDataLayout();

  SmtxSlampSpeculationManager &smtxMan = getAnalysis< SmtxSlampSpeculationManager >();
  smtxslampaa = new SmtxSlampAA(&smtxMan);
  smtxslampaa->InitializeLoopAA(this, DL);

  // Control Speculation
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(A->getHeader());
  edgeaa = new EdgeCountOracle(ctrlspec);
  edgeaa->InitializeLoopAA(this, DL);
}

void SmtxSlampSelector::destroySpeculativeAnalysisStack()
{
  delete edgeaa;
  delete smtxslampaa;

  ControlSpeculation *ctrlspec = getControlSpeculation();
  ctrlspec->setLoopOfInterest(0);
}

ControlSpeculation *SmtxSlampSelector::getControlSpeculation() const
{
  return getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
}

PredictionSpeculation *SmtxSlampSelector::getPredictionSpeculation() const
{
  return &getAnalysis< HeaderPhiPredictionSpeculation >();
}

void SmtxSlampSelector::resetAfterInline(
  Instruction *callsite_no_longer_exists,
  Function *caller,
  Function *callee,
  const ValueToValueMapTy &vmap,
  const CallsPromotedToInvoke &call2invoke)
{
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  LAMPLoadProfile &lampprof = getAnalysis< LAMPLoadProfile >();
  SmtxSpeculationManager &smtxMan = getAnalysis< SmtxSpeculationManager >();

  UpdateLAMP lamp( lampprof );
  lamp.resetAfterInline(callsite_no_longer_exists, caller, callee, vmap, call2invoke);

  ctrlspec->reset();
  smtxMan.reset();
}

bool SmtxSlampSelector::runOnModule(Module &mod)
{
  DEBUG_WITH_TYPE("classify",
    errs() << "#################################################\n"
           << " SMTX Selection (using SLAMP)\n\n\n");

  Vertices vertices;
  Edges edges;
  VertexWeights weights;
  VertexSet maxClique;

  doSelection(vertices, edges, weights, maxClique);

  return false;
}

char SmtxSlampSelector::ID = 0;
static RegisterPass< SmtxSlampSelector > rp("spec-priv-smtx-slamp-selector", "Smtx Selector using SLAMP");
static RegisterAnalysisGroup< Selector > link(rp);


}
}
