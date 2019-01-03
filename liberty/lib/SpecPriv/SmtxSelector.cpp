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
#include "SmtxSelector.h"
#include "UpdateOnCloneAdaptors.h"
#include "HeaderPhiPredictionSpeculation.h"


namespace liberty
{
namespace SpecPriv
{

void SmtxSelector::getAnalysisUsage(AnalysisUsage &au) const
{
  Selector::analysisUsage(au);

  au.addRequired< LAMPLoadProfile >();
  au.addRequired< SLAMPLoadProfile >();
  au.addRequired< SmtxSpeculationManager >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< HeaderPhiPredictionSpeculation >();
}

void SmtxSelector::buildSpeculativeAnalysisStack(const Loop *A)
{
  Module *mod = A->getHeader()->getParent()->getParent();
  const DataLayout &td = mod->getDataLayout();

  SmtxSpeculationManager &smtxMan = getAnalysis< SmtxSpeculationManager >();
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();


  SLAMPLoadProfile& slamp = getAnalysis< SLAMPLoadProfile >();

  smtxaa = new SmtxAA(&smtxMan);
  smtxaa->InitializeLoopAA(this, td);

  slampaa = new SlampOracle(&slamp);
  slampaa->InitializeLoopAA(this, td);

  // Control Speculation
  ctrlspec->setLoopOfInterest(A->getHeader());
  edgeaa = new EdgeCountOracle(ctrlspec);
  edgeaa->InitializeLoopAA(this, td);
}

void SmtxSelector::destroySpeculativeAnalysisStack()
{
  delete edgeaa;
  delete slampaa;
  delete smtxaa;

  ControlSpeculation *ctrlspec = getControlSpeculation();
  ctrlspec->setLoopOfInterest(0);
}

ControlSpeculation *SmtxSelector::getControlSpeculation() const
{
  return getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
}

PredictionSpeculation *SmtxSelector::getPredictionSpeculation() const
{
  return &getAnalysis< HeaderPhiPredictionSpeculation >();
}

void SmtxSelector::resetAfterInline(
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

bool SmtxSelector::runOnModule(Module &mod)
{
  DEBUG_WITH_TYPE("classify",
    errs() << "#################################################\n"
           << " SMTX Selection\n\n\n");

  Vertices vertices;
  Edges edges;
  VertexWeights weights;
  VertexSet maxClique;

  doSelection(vertices, edges, weights, maxClique);

  return false;
}

char SmtxSelector::ID = 0;
static RegisterPass< SmtxSelector > rp("spec-priv-smtx-selector", "Smtx Selector");
static RegisterAnalysisGroup< Selector > link(rp);


}
}
