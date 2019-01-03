#define DEBUG_TYPE "selector"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/Pipeline.h"
#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/ProfilePerformanceEstimator.h"
#include "liberty/SpecPriv/Read.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/ModuleLoops.h"

#include "RoI.h"
#include "Smtx2Selector.h"
#include "SmtxManager.h"
#include "UpdateOnCloneAdaptors.h"
//#include "Transform.h"


namespace liberty
{
namespace SpecPriv
{

const HeapAssignment &Smtx2Selector::getAssignment() const { return assignment; }
HeapAssignment &Smtx2Selector::getAssignment() { return assignment; }

void Smtx2Selector::getAnalysisUsage(AnalysisUsage &au) const
{
  Selector::analysisUsage(au);

  //au.addRequired< ProfileInfo >();
  au.addRequired< BlockFrequencyInfoWrapperPass >();
  au.addRequired< BranchProbabilityInfoWrapperPass >();
  au.addRequired< LAMPLoadProfile >();
  au.addRequired< ReadPass >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< ProfileGuidedPredictionSpeculator >();
  au.addRequired< PtrResidueSpeculationManager >();
  au.addRequired< SmtxSpeculationManager >();
  au.addRequired< Classify >();
}

void Smtx2Selector::computeVertices(Vertices &vertices)
{
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  const Classify &classify = getAnalysis< Classify >();
  for(Classify::iterator i=classify.begin(), e=classify.end(); i!=e; ++i)
  {
    const BasicBlock *header = i->first;
    Function *fcn = const_cast< Function * >(header->getParent() );

    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);
    Loop *loop = li.getLoopFor(header);
    assert( loop->getHeader() == header );

    const HeapAssignment &asgn = i->second;
    if( ! asgn.isValidFor(loop) )
      continue;

    vertices.push_back( loop );
  }
}


bool Smtx2Selector::compatibleParallelizations(const Loop *A, const Loop *B) const
{
  const Classify &classify = getAnalysis< Classify >();

  const HeapAssignment &asgnA = classify.getAssignmentFor(A);
  assert( asgnA.isValidFor(A) );

  const HeapAssignment &asgnB = classify.getAssignmentFor(B);
  assert( asgnB.isValidFor(B) );

  return compatible(asgnA, asgnB);
}

void Smtx2Selector::buildSpeculativeAnalysisStack(const Loop *A)
{
  Module *mod = A->getHeader()->getParent()->getParent();
  //DataLayout &td = getAnalysis< DataLayout >();
  const DataLayout &td = mod->getDataLayout();
  SmtxSpeculationManager &smtxMan = getAnalysis< SmtxSpeculationManager >();
  PtrResidueSpeculationManager &prman = getAnalysis< PtrResidueSpeculationManager >();
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ProfileGuidedPredictionSpeculator &predspec = getAnalysis< ProfileGuidedPredictionSpeculator >();
  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
  Classify &classify = getAnalysis< Classify >();

  const HeapAssignment &asgnA = classify.getAssignmentFor(A);
  assert( asgnA.isValidFor(A) );

  // SMTX-style flow-cutting speculation
  smtxaa = new SmtxAA(&smtxMan);
  smtxaa->InitializeLoopAA(this, td);

  // Pointer-residue speculation
  residueaa = new PtrResidueAA(td,prman);
  residueaa->InitializeLoopAA(this, td);

  // Value predictions
  predaa = new PredictionAA(&predspec);
  predaa->InitializeLoopAA(this, td);

  // Control Speculation
  ctrlspec->setLoopOfInterest(A->getHeader());
  edgeaa = new EdgeCountOracle(ctrlspec);
  edgeaa->InitializeLoopAA(this, td);

  // Apply locality reasoning (i.e. an object is local/private to a context,
  // and thus cannot source/sink loop-carried deps).
  localityaa = new LocalityAA(spresults,asgnA);
  localityaa->InitializeLoopAA(this, td);
}

void Smtx2Selector::destroySpeculativeAnalysisStack()
{
  delete localityaa;
  delete edgeaa;
  delete predaa;
  delete residueaa;
  delete smtxaa;

  ControlSpeculation *ctrlspec = getControlSpeculation();
  ctrlspec->setLoopOfInterest(0);
}

PredictionSpeculation *Smtx2Selector::getPredictionSpeculation() const
{
  return &getAnalysis< ProfileGuidedPredictionSpeculator >();
}

ControlSpeculation *Smtx2Selector::getControlSpeculation() const
{
  return getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
}

void Smtx2Selector::resetAfterInline(
  Instruction *callsite_no_longer_exists,
  Function *caller,
  Function *callee,
  const ValueToValueMapTy &vmap,
  const CallsPromotedToInvoke &call2invoke)
{
  Classify &classify = getAnalysis< Classify >();
  PtrResidueSpeculationManager &prman = getAnalysis< PtrResidueSpeculationManager >();
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ProfileGuidedPredictionSpeculator &predspec = getAnalysis< ProfileGuidedPredictionSpeculator >();
  LAMPLoadProfile &lampprof = getAnalysis< LAMPLoadProfile >();
  SmtxSpeculationManager &smtxMan = getAnalysis< SmtxSpeculationManager >();
  Read &spresults = getAnalysis< ReadPass >().getProfileInfo();

  UpdateLAMP lamp( lampprof );

  UpdateGroup group;
  group.add( &spresults );
  group.add( &classify );


  FoldManager &fmgr = * spresults.getFoldManager();

  // Hard to identify exactly which context we're updating,
  // since the context includes loops and functions, but not callsites.

  // Find every context in which 'callee' is called by 'caller'
  typedef std::vector<const Ctx *> Ctxs;
  Ctxs affectedContexts;
  for(FoldManager::ctx_iterator k=fmgr.ctx_begin(), z=fmgr.ctx_end(); k!=z; ++k)
  {
    const Ctx *ctx = &*k;
    if( ctx->type != Ctx_Fcn )
      continue;
    if( ctx->fcn != callee )
      continue;

    if( !ctx->parent )
      continue;
    if( ctx->parent->getFcn() != caller )
      continue;

    affectedContexts.push_back( ctx );
    errs() << "Affected context: " << *ctx << '\n';
  }

  // Inline those contexts to build the cmap, amap
  CtxToCtxMap cmap;
  AuToAuMap amap;
  for(Ctxs::const_iterator k=affectedContexts.begin(), z=affectedContexts.end(); k!=z; ++k)
    fmgr.inlineContext(*k,vmap,cmap,amap);

  lamp.resetAfterInline(callsite_no_longer_exists, caller, callee, vmap, call2invoke);

  // Update others w.r.t each of those contexts
  for(Ctxs::const_iterator k=affectedContexts.begin(), z=affectedContexts.end(); k!=z; ++k)
    group.contextRenamedViaClone(*k,vmap,cmap,amap);

  spresults.removeInstruction( callsite_no_longer_exists );
  predspec.reset();
  prman.reset();
  ctrlspec->reset();
  smtxMan.reset();
}


bool Smtx2Selector::runOnModule(Module &mod)
{
  DEBUG_WITH_TYPE("classify",
    errs() << "#################################################\n"
           << " SMTX2 Selection\n\n\n");

  Vertices vertices;
  Edges edges;
  VertexWeights weights;
  VertexSet maxClique;

  doSelection(vertices, edges, weights, maxClique);

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
  return false;
}

void Smtx2Selector::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - Selector::contextRenamedViaClone: " << *changedContext << '\n';
  assignment.contextRenamedViaClone(changedContext,vmap,cmap,amap);
  Selector::contextRenamedViaClone(changedContext,vmap,cmap,amap);
}

char Smtx2Selector::ID = 0;
static RegisterPass< Smtx2Selector > aninstance("smtx2-selector", "SMTX2 Selector");
static RegisterAnalysisGroup< Selector > link(aninstance);

}
}
