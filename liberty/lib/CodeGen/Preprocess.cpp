#define DEBUG_TYPE "specpriv-transform"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Speculation/SmtxSlampManager.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Selector.h"
#include "liberty/Redux/Reduction.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"
#include "liberty/Utilities/SplitEdge.h"
#include "liberty/Utilities/StaticID.h"
#include "liberty/Utilities/Timer.h"

#include "liberty/Speculation/Api.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Discriminator.h"
//#include "liberty/Speculation/PtrResidueManager.h"
#include "liberty/Speculation/HeaderPhiPredictionSpeculation.h"
#include "liberty/CodeGen/Preprocess.h"
//#include "PrivateerSelector.h"
#include "liberty/Speculation/RemedSelector.h"
//#include "NoSpecSelector.h"
#include "liberty/Speculation/SmtxManager.h"
//#include "SmtxSelector.h"
//#include "Smtx2Selector.h"
#include "liberty/Speculation/Recovery.h"
#include "liberty/CodeGen/RoI.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numLiveOuts,      "Live-out values demoted to private memory");
STATISTIC(numReduxLiveOuts, "Redux live-out values demoted to redux memory");

void Preprocess::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< StaticID >();
  au.addRequired< ModuleLoops >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< Selector >();

  au.addPreserved< ReadPass >();
  au.addPreserved< ModuleLoops >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
  au.addPreserved< ProfileGuidedPredictionSpeculator >();
  //au.addPreserved< PtrResidueSpeculationManager >();
  au.addPreserved< SmtxSpeculationManager >();
  au.addPreserved< HeaderPhiPredictionSpeculation >();
  au.addPreserved< SmtxSlampSpeculationManager >();
  au.addPreserved< Selector >();
  //au.addPreserved< NoSpecSelector >();
  //au.addPreserved< SpecPrivSelector >();
  au.addPreserved< RemedSelector >();
  //au.addPreserved< SmtxSelector >();
  //au.addPreserved< Smtx2Selector >();
}


void Preprocess::addToLPS(Instruction *newInst, Instruction *gravity)
{
  Selector &selector = getAnalysis< Selector >();
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    LoopParallelizationStrategy *lps = i->second.get();
    lps->addInstruction(newInst,gravity);
  }
}

void Preprocess::replaceInLPS(Instruction *newInst, Instruction *oldInst)
{
  Selector &selector = getAnalysis< Selector >();
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    LoopParallelizationStrategy *lps = i->second.get();
    lps->replaceInstruction(newInst,oldInst);
  }
}

void Preprocess::getExecutingStages(Instruction* inst, std::vector<unsigned>& stages)
{
  Selector &selector = getAnalysis< Selector >();
  LoopParallelizationStrategy* included_strategy = NULL;

  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    LoopParallelizationStrategy *lps = i->second.get();

    // Do we have DoallStrategy implementation?
    assert(lps->getKind() == LoopParallelizationStrategy::LPSK_Pipeline);

    std::vector<unsigned> s;
    lps->getExecutingStages(inst, s);

    if (s.empty()) continue;

    if (included_strategy)
    {
      errs() << "Error: inst included in more than 2 strategies - "; inst->dump();
      stages.clear();
      return;
    }

    stages.insert(stages.begin(), s.begin(), s.end());
  }
}

bool Preprocess::ifI2IsInI1IsIn(Instruction* i1, Instruction* i2)
{
  Selector &selector = getAnalysis< Selector >();
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    LoopParallelizationStrategy *lps = i->second.get();
    if ( !lps->ifI2IsInI1IsIn(i1, i2) )
      return false;
  }
  return true;
}

bool Preprocess::addInitializationFunction()
{
  // OUTSIDE of parallel region
  Function *init = Function::Create(fv2v, GlobalValue::InternalLinkage, "__specpriv_startup", mod);
  LLVMContext &ctx = mod->getContext();
  BasicBlock *entry = BasicBlock::Create(ctx, "entry", init);

  Constant *beginfcn = Api(mod).getGenericBegin();
  InstInsertPt::End(entry) << CallInst::Create(beginfcn);

  Constant *beginspecfcn = Api(mod).getBegin();
  InstInsertPt::End(entry) << CallInst::Create(beginspecfcn);

  Constant *allocQueues = Api(mod).getAllocQueues();
  InstInsertPt::End(entry) << CallInst::Create(
      allocQueues, ConstantInt::get(u32, loops.size()));

  Constant *allocStrat = Api(mod).getAllocStratInfo();
  InstInsertPt::End(entry) << CallInst::Create(
      allocStrat, ConstantInt::get(u32, loops.size()));

  Constant *beginspawnworkfcn = Api(mod).getSpawnWorkersBegin();
  auto spawnworkersCall = CallInst::Create(beginspawnworkfcn);
  InstInsertPt::End(entry) << spawnworkersCall;

  initFcn = InstInsertPt::Before(spawnworkersCall);

  auto retInst = ReturnInst::Create(ctx, entry);
  callBeforeMain( init );

  return true;
}

bool Preprocess::addFinalizationFunction()
{
  // OUTSIDE of parallel region
  Function *fini = Function::Create(fv2v, GlobalValue::InternalLinkage, "__specpriv_shutdown", mod);
  LLVMContext &ctx = mod->getContext();
  BasicBlock *entry = BasicBlock::Create(ctx, "entry", fini);

  Constant *endfcn = Api(mod).getGenericEnd();
  InstInsertPt::End(entry) << CallInst::Create(endfcn);

  Constant *endspecfcn = Api(mod).getEnd();
  InstInsertPt::End(entry) << CallInst::Create(endspecfcn);

  Constant *freequeues = Api(mod).getFreeQueues();
  InstInsertPt::End(entry) << CallInst::Create(freequeues);

  InstInsertPt::End(entry) << CallInst::Create(Api(mod).getCleanupStrategy());

  auto retInst = ReturnInst::Create(ctx, entry);
  finiFcn = InstInsertPt::Before(retInst);
  callAfterMain( fini );

  return true;
}



void Preprocess::assert_strategies_consistent_with_ir()
{
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();

  Selector &selector = getAnalysis< Selector >();

  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    BasicBlock *header = i->first;
    LoopParallelizationStrategy &lps = *i->second;

    Function *fcn = header->getParent();
    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);
    Loop *loop = li.getLoopFor(header);
    loop->verifyLoop();

    lps.assertConsistentWithIR(loop);
  }
}

bool Preprocess::runOnModule(Module &module)
{
  DEBUG(errs() << "#################################################\n"
               << " Preprocess\n\n\n");
  mod = &module;
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  init(mloops);

  if( loops.empty() )
    return false;

  assert_strategies_consistent_with_ir();
  bool modified = false;

  // Make a global constructor function
  modified |= addInitializationFunction();
  modified |= addFinalizationFunction();

  // Modify the code so that all register live-outs
  // and all loop-carried deps are stored into a private AU.
  for(unsigned i=0; i<loops.size(); ++i)
  {
    Loop *loop = loops[i];

    LiveoutStructure liveouts;

    modified |= demoteLiveOutsAndPhis(loop, liveouts, mloops);
    recovery.getRecoveryFunction(loop, mloops, liveouts);
    modified = true;
  }

  if( modified )
    assert_strategies_consistent_with_ir();

  // Duplicate code, as necessary, so that each instance
  // can be specialized according to parallel/non-parallel
  // regions
  modified |= fixStaticContexts();

  if( modified )
    assert_strategies_consistent_with_ir();

  if( modified )
  {
    // Invalidate LoopInfo-analysis, since we have changed
    // the code.
    for(RoI::FSet::iterator i=roi.fcns.begin(), e=roi.fcns.end(); i!=e; ++i)
      mloops.forget(*i);

    assert_strategies_consistent_with_ir();
    DEBUG(errs() << "Successfully applied speculation to sequential IR\n");
  }

  return modified;
}

bool Preprocess::fixStaticContexts()
{
  bool modified = false;

  // Resolve statically-ambiguous contexts by
  // duplicating tails of the callgraph, when
  // necessary.
  ReadPass *rp = getAnalysisIfAvailable< ReadPass >();
  Selector *sps = getAnalysisIfAvailable< Selector >();

  Selector &selector = getAnalysis< Selector >();
  UpdateGroup group;
  {
    // If we are doing Spec-priv speculation,
    // We must ensure that we update this too.
    if( rp )
    {
      const Read &read = rp->getProfileInfo();
      group.add( const_cast<Read*>( &read ) );
    }

    group.add( &selector );

    ProfileGuidedControlSpeculator &ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >();
    group.add( &ctrlspec );

    ProfileGuidedPredictionSpeculator *predspec = getAnalysisIfAvailable< ProfileGuidedPredictionSpeculator >();
    if( predspec )
      group.add( predspec );

    //PtrResidueSpeculationManager *prman = getAnalysisIfAvailable< PtrResidueSpeculationManager >();
    //if( prman )
    //  group.add( prman );

    SmtxSpeculationManager *smtxMan = getAnalysisIfAvailable< SmtxSpeculationManager >();
    if( smtxMan )
      group.add(smtxMan);
  }

  // ONLY for spec-priv speculation.
  // Mark the live-out structure as PRIVATE
  FoldManager local_fmgr;
  FoldManager *fmgr = &local_fmgr;
  if( rp && sps )
  {
    const Read &read = rp->getProfileInfo();
    FoldManager *fmgr = read.getFoldManager();

    const HeapAssignment &asgn = sps->getAssignment();
    Discriminator discrim(asgn);
    modified |= discrim.resolveAmbiguitiesViaCloning(group, *fmgr);

    DEBUG(errs() << asgn);
  }

  // RoI: collect set of fcns/bbs reachable from the
  // parallel region.  This doesn't include the
  // fcns which contain the top-level loops (unless
  // the parallel regions call them).
  roi.clear();
  for(unsigned i=0; i<loops.size(); ++i)
  {
    Loop *loop = loops[i];
    roi.addRoots( loop->block_begin(), loop->block_end() );
    roi.sweep( loop->block_begin(), loop->block_end() );
  }

  // Clone functions as necessary so that there are
  // no side-entrances to the RoI.  In other words,
  // blocks in the RoI may assume they are within
  // a parallel region.

  set< pair<Instruction*, Instruction*> > newInsts;
  modified |= roi.resolveSideEntrances(group, *fmgr, getAnalysis<Selector>());

  // Add the functions which contains top-level loops
  // to the RoI.
  for(unsigned i=0; i<loops.size(); ++i)
    roi.fcns.insert( loops[i]->getHeader()->getParent() );

  DEBUG(
    errs() << "RoI consists of " << roi.bbs.size() << " bbs across " << roi.fcns.size() << " functions\n";
    for(RoI::FSet::const_iterator i=roi.fcns.begin(), e=roi.fcns.end(); i!=e; ++i)
      errs() << "  - " << (*i)->getName() << '\n';
  );

  return modified;
}

const RecoveryFunction &Preprocess::getRecoveryFunction(Loop *loop) const
{
  return recovery.getRecoveryFunction(loop);
}

void Preprocess::init(ModuleLoops &mloops)
{
  LLVMContext &ctx = mod->getContext();

  voidty = Type::getVoidTy(ctx);
  u8 = Type::getInt8Ty(ctx);
  u16 = Type::getInt16Ty(ctx);
  u32 = Type::getInt32Ty(ctx);
  u64 = Type::getInt64Ty(ctx);
  voidptr = PointerType::getUnqual(u8);

  std::vector<Type *> formals;
  fv2v = FunctionType::get(voidty, formals, false);

  Selector &selector = getAnalysis<Selector>();

  // Identify loops we will parallelize
  for (Selector::strat_iterator i = selector.strat_begin(),
                                e = selector.strat_end();
       i != e; ++i) {
    BasicBlock *header = i->first;
    Function *fcn = const_cast<Function *>(header->getParent());

    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);
    Loop *loop = li.getLoopFor(header);
    assert(loop->getHeader() == header);

    loops.push_back(loop);

    DEBUG(errs() << " - loop " << fcn->getName() << " :: " << header->getName()
                 << "\n");

    // populate selectedCtrlSpecDeps
    auto &loop2SelectedRemedies = selector.getLoop2SelectedRemedies();
    SelectedRemedies *selectedRemeds = loop2SelectedRemedies[header].get();

    bool specUsedFlag = false;
    for (auto &remed : *selectedRemeds) {
      if (remed->getRemedyName().equals("ctrl-spec-remedy")) {
        ControlSpecRemedy *ctrlSpecRemed = (ControlSpecRemedy *)&*remed;
        if (!ctrlSpecRemed->brI)
          continue;
        if (const TerminatorInst *term =
                dyn_cast<TerminatorInst>(ctrlSpecRemed->brI)) {
          selectedCtrlSpecDeps[header].insert(term);
        }
        specUsedFlag = true;
      }
      if (remed->getRemedyName().equals("locality-remedy")) {
        if (!separationSpecUsed.count(header))
          separationSpecUsed.insert(header);
        specUsedFlag = true;
      }
      if (remed->getRemedyName().equals("smtx-slamp-remed") ||
          remed->getRemedyName().equals("smtx-lamp-remed") ||
          remed->getRemedyName().equals("loaded-value-pred-remed")) {
        specUsedFlag = true;
      }
      if (remed->getRemedyName().equals("redux-remedy")) {
        ReduxRemedy *reduxRemed = (ReduxRemedy *)&*remed;
        const Instruction *liveOutV = reduxRemed->liveOutV;
        reduxV.insert(liveOutV);
        redux2Type[liveOutV] = reduxRemed->type;
      }
    }
    if (specUsedFlag)
      specUsed.insert(header);
  }
}

void Preprocess::replaceLiveOutUsage(Instruction *def, unsigned i, Loop *loop,
                                     StringRef name, Instruction *object,
                                     bool redux) {

  Value *zero = ConstantInt::get(u32, 0);
  Value *indices[] = {zero, ConstantInt::get(u32, i)};
  std::vector<User *> users(def->user_begin(), def->user_end());
  for (unsigned j = 0; j < users.size(); ++j)
    if (Instruction *user = dyn_cast<Instruction>(users[j])) {
      if (loop->contains(user))
        continue;

      LoadInst *load;
      // Either the use is a PHI, or not.
      if (PHINode *phi = dyn_cast<PHINode>(user)) {
        // It is a phi; we must split an edge :(
        for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k) {
          if (phi->getIncomingValue(k) != def)
            continue;

          BasicBlock *pred = phi->getIncomingBlock(k);
          BasicBlock *succ = phi->getParent();

          BasicBlock *splitedge = split(pred, succ, (".unspill-" + Twine(name) + ".").str());

          LoadInst *load;
          if (!redux) {
            GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(
                object, ArrayRef<Value *>(&indices[0], &indices[2]));
            gep->setName(name + ":" + def->getName());
            load = new LoadInst(gep);
            InstInsertPt::Before(user) << gep;
          } else {
            load = new LoadInst(object);
          }

          InstInsertPt::Beginning(splitedge) << load;
          phi->setIncomingValue(k, load);
        }
      } else {
        if (!redux) {
          GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(
              object, ArrayRef<Value *>(&indices[0], &indices[2]));
          gep->setName(name + ":" + def->getName());
          load = new LoadInst(gep);
          InstInsertPt::Before(user) << gep;
        } else {
          load = new LoadInst(object);
        }

        // Simple case: not a phi.
        InstInsertPt::Before(user) << load;
        user->replaceUsesOfWith(def, load);
      }
    }
}

bool Preprocess::demoteLiveOutsAndPhis(Loop *loop, LiveoutStructure &liveoutStructure, ModuleLoops &mloops)
{
  // Identify a unique set of instructions within this loop
  // whose value is used outside of the loop.
  std::set<Instruction*> liveoutSet;

  // keep reduxLiveOuts as a subset of reduxV just in case some redux variables
  // are actually not live-out but for some reason were not eliminated as dead
  // code
  std::unordered_set<Instruction*> reduxLiveoutSet;

  // The reducible liveouts, in a fixed order.
  LiveoutStructure::IList &reduxLiveouts = liveoutStructure.reduxLiveouts;

  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      for(Value::user_iterator k=inst->user_begin(), f=inst->user_end(); k!=f; ++k)
        if( Instruction *user = dyn_cast< Instruction >( *k ) )
          if( ! loop->contains(user) ) {
            if (reduxV.count(inst)) {
              reduxLiveoutSet.insert(inst);
              reduxLiveouts.push_back(inst);
            } else
              liveoutSet.insert(inst);
          }
    }
  }

  // Additionally, for recovery we will need the loop carried values.
  // Specifically, the values defined within the loop which are incoming
  // to a PHI.  We exclude the canonical induction variable, since we
  // handle that specially.
  LiveoutStructure::PhiList &phis = liveoutStructure.phis;
  BasicBlock *header = loop->getHeader();
  for (BasicBlock::iterator j = header->begin(), z = header->end(); j != z;
       ++j) {
    PHINode *phi = dyn_cast<PHINode>(&*j);
    if (!phi)
      break;

    // exclude induction variable and reduction variables
    if (indVarPhi != phi && !reduxLiveoutSet.count(phi))
      phis.push_back(phi);

  }

  liveoutStructure.type = 0;
  liveoutStructure.object = 0;

  const unsigned N = liveoutSet.size();
  const unsigned M = phis.size();
  const unsigned K = reduxLiveoutSet.size();
  if( N + M + K < 1 )
    return false;

  // The liveouts, in a fixed order.
  LiveoutStructure::IList &liveouts = liveoutStructure.liveouts;
  liveouts.insert( liveouts.end(),
    liveoutSet.begin(), liveoutSet.end() );

  // Allocate a structure on the stack to hold all live-outs.
  LLVMContext &ctx = mod->getContext();
  Value *zero = ConstantInt::get(u32, 0);
  std::vector<Type *> fields( N + M );
  for(unsigned i=0; i<N; ++i)
    fields[i] = liveouts[i]->getType();
  for(unsigned i=0; i<M; ++i)
    fields[N+i] = phis[i]->getType();
  StructType *structty = liveoutStructure.type = StructType::get(ctx, fields);

  Function *fcn = loop->getHeader()->getParent();
  AllocaInst *liveoutObject = new AllocaInst(
      structty, 0, "liveouts.from." + loop->getHeader()->getName());
  liveoutStructure.object = liveoutObject;
  InstInsertPt::Beginning(fcn) << liveoutObject;

  DEBUG(errs() << "Adding a liveout object " << *liveoutObject
               << " to function " << fcn->getName() << '\n');

  // Allocate a global variable to hold each reducible live-out
  for(unsigned i=0; i<K; ++i) {
    Type *pty = reduxLiveouts[i]->getType();
    AllocaInst *reduxObject =
        new AllocaInst(pty, 0,
                       "reduxLiveout.from." + loop->getHeader()->getName() +
                           "." + reduxLiveouts[i]->getName());
    liveoutStructure.reduxObjects.push_back(reduxObject);
    InstInsertPt::Beginning(fcn) << reduxObject;
  }

  // Identify the edges at the end of an iteration
  // == loop backedges, loop exits.
  typedef std::vector< RecoveryFunction::CtrlEdge > CtrlEdges;
  CtrlEdges iterationBounds;
  CtrlEdges loopBounds;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();
    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      BasicBlock *dest = term->getSuccessor(sn);

      // Loop back edge
      if( dest == header )
        iterationBounds.push_back( RecoveryFunction::CtrlEdge(term,sn) );

      // Loop exit
      else if( ! loop->contains(dest) )
        loopBounds.push_back( RecoveryFunction::CtrlEdge(term,sn) );
    }
  }

  // After each definition of a live-out value, store it into the structure.
  for(unsigned i=0; i<N; ++i)
  {
    Instruction *def = liveouts[i];

    Value *indices[] = { zero, ConstantInt::get(u32, i) };
    GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(liveoutObject, ArrayRef<Value*>(&indices[0], &indices[2]));
    gep->setName( "liveout:" + def->getName() );
    StoreInst *store = new StoreInst(def, gep);

    InstInsertPt::After(def) << gep << store;

    // Add these new instructions to the partition
    addToLPS(gep, def);
    addToLPS(store, def);
  }

  LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);

  // store incoming values to header for all phis and redux variables
  //
  // MTCG will limit stores inside the loop only when checkpoint is imminent.
  // isolate stores within the loop in a new basic block.
  //
  // if no spec or no reducible variables then no reason to store whatsover
  if (K > 0 || (M > 0 && specUsed.count(header))) {
    for (unsigned i = 0, Nib = iterationBounds.size(); i < Nib; ++i) {
      TerminatorInst *term = iterationBounds[i].first;
      BasicBlock *source = term->getParent();
      unsigned sn = iterationBounds[i].second;
      BasicBlock *dest = term->getSuccessor(sn);

      BasicBlock *save_redux_lc = BasicBlock::Create(ctx, "save.redux.lc", fcn);

      // Update PHIs in dest
      for (BasicBlock::iterator j = dest->begin(), z = dest->end(); j != z;
           ++j) {
        PHINode *phi = dyn_cast<PHINode>(&*j);
        if (!phi)
          break;

        int idx = phi->getBasicBlockIndex(source);
        if (idx != -1)
          phi->setIncomingBlock(idx, save_redux_lc);
      }

      term->setSuccessor(sn, save_redux_lc);
      save_redux_lc->moveAfter(source);
      Instruction *br = BranchInst::Create(dest, save_redux_lc);

      loop->addBasicBlockToLoop(save_redux_lc, li);

      Instruction *gravity;
      if (K > 0)
        gravity = reduxLiveouts[0];
      else
        gravity = phis[0];
      addToLPS(br, gravity);
    }
  }

  for (unsigned i = 0; i < M; ++i) {
    PHINode *phi = phis[i];
    Value *indices[] = {zero, ConstantInt::get(u32, N + i)};

    for (unsigned j = 0; j < phi->getNumIncomingValues(); ++j) {
      BasicBlock *pred = phi->getIncomingBlock(j);

      // if no spec then no need for recovery; thus no need to store
      // loop-carried values inside the loop
      if (!specUsed.count(header) && loop->contains(pred))
        continue;

      GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(
          liveoutObject, ArrayRef<Value *>(&indices[0], &indices[2]));
      gep->setName("phi-incoming:" + phi->getName());
      Value *vdef = phi->getIncomingValue(j);
      StoreInst *store = new StoreInst(vdef, gep);
      InstInsertPt::End(pred) << gep << store;

      Instruction *gravity = phi;
      if (Instruction *idef = dyn_cast<Instruction>(vdef))
        gravity = idef;

      if (loop->contains(pred)) {
        addToLPS(gep, gravity);
        addToLPS(store, gravity);
      }
    }
  }
  for (unsigned i = 0; i < K; ++i) {
    Instruction *reduxI = reduxLiveouts[i];
    PHINode *phi = dyn_cast<PHINode>(reduxI);
    assert(phi && "Redux variable not a phi?");

    for (unsigned j = 0; j < phi->getNumIncomingValues(); ++j) {
      BasicBlock *pred = phi->getIncomingBlock(j);

      Value *vdef = phi->getIncomingValue(j);
      StoreInst *store = new StoreInst(vdef, liveoutStructure.reduxObjects[i]);
      InstInsertPt::End(pred) << store;

      Instruction *gravity = phi;
      if (Instruction *idef = dyn_cast<Instruction>(vdef))
        gravity = idef;

      if (loop->contains(pred)) {
        addToLPS(store, gravity);
      }
    }
  }

  // TODO: replace loads from/stores to this structure with
  // API calls; allow the runtime to implement private semantics
  // as necessary.

  // Before each use of the live-out values which is NOT within the loop,
  // load it from the structure.
  // This /also/ means that the liveout structure must
  // have private semantics.
  for(unsigned i=0; i<N; ++i)
  {
    Instruction *def = liveouts[i];
    replaceLiveOutUsage(def, i, loop, "liveout", liveoutObject, false);
  }
  // redux live-out values
  for(unsigned i=0; i<K; ++i)
  {
    Instruction *def = reduxLiveouts[i];
    replaceLiveOutUsage(def, i, loop, "reduxLiveout", liveoutStructure.reduxObjects[i], true);
  }

  // store reducible live-outs on loop exits
  for (unsigned i = 0; i < loopBounds.size(); ++i) {
    TerminatorInst *term = loopBounds[i].first;
    BasicBlock *source = term->getParent();
    unsigned sn = loopBounds[i].second;
    BasicBlock *dest = term->getSuccessor(sn);

    {
      Twine bbname = "end.loop." + Twine(i);
      BasicBlock *split = BasicBlock::Create(ctx, bbname, fcn);

      // Update PHIs in dest
      for (BasicBlock::iterator j = dest->begin(), z = dest->end(); j != z;
           ++j) {
        PHINode *phi = dyn_cast<PHINode>(&*j);
        if (!phi)
          break;

        int idx = phi->getBasicBlockIndex(source);
        if (idx != -1)
          phi->setIncomingBlock(idx, split);
      }
      term->setSuccessor(sn, split);
      split->moveAfter(source);

      for (unsigned k = 0; k < K; ++k) {
        Instruction *def = reduxLiveouts[k];

        StoreInst *store = new StoreInst(def, liveoutStructure.reduxObjects[k]);

        InstInsertPt::End(split) << store;

        // Add these new instructions to the partition
        addToLPS(store, def);
      }

      BranchInst::Create(dest, split);
      if (K > 0)
        addToLPS(split->getTerminator(), reduxLiveouts[0]);
    }
  }

  // The liveout structure must have private
  // semantics.  We rely on the runtime to accomplish
  // that.  With SMTX, this is automatic.
  // With Specpriv, we must mark that structure
  // as a member of the PRIVATE heap.
  // The redux liveout structure needs to be a member of the redux heap
  ReadPass *rp = getAnalysisIfAvailable< ReadPass >();
  Selector *sps = getAnalysisIfAvailable< Selector >();
  if( rp && sps )
  {
    const Read &spresults = rp->getProfileInfo();
    Ctx *fcn_ctx = spresults.getCtx(fcn);
    HeapAssignment &asgn = sps->getAssignment();

    // liveout (non-redux) -> private
    Ptrs aus;
    assert(spresults.getUnderlyingAUs(liveoutObject, fcn_ctx, aus) &&
           "Failed to create AU objects for the live-out object?!");
    HeapAssignment::AUSet &privs = asgn.getPrivateAUs();
    for (Ptrs::iterator i = aus.begin(), e = aus.end(); i != e; ++i)
      privs.insert(i->au);

    // redux liveout -> redux
    HeapAssignment::ReduxAUSet &reduxs = asgn.getReductionAUs();
    for (unsigned i = 0; i < K; ++i) {
      Ptrs reduxaus;
      assert(spresults.getUnderlyingAUs(liveoutStructure.reduxObjects[i],
                                        fcn_ctx, reduxaus) &&
             "Failed to create AU objects for the redux live-out object?!");
      for (Ptrs::iterator p = reduxaus.begin(), e = reduxaus.end(); p != e; ++p)
        reduxs[p->au] = redux2Type[reduxLiveouts[i]];
    }
  }

  numLiveOuts += N;
  numReduxLiveOuts += K;

  return true;
}

char Preprocess::ID = 0;
static RegisterPass<Preprocess> x("spec-priv-preprocess",
  "Preprocess RoI");
}
}

