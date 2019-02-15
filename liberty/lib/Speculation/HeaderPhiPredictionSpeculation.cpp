#define DEBUG_TYPE "header-phi-prediction-speculation"

#include "liberty/Speculation/HeaderPhiPredictionSpeculation.h"
#include "liberty/Speculation/HeaderPhiLoad.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/Utilities/ComputeGEPOffset.h"
#include "Metadata.h"
#include "llvm/IR/Dominators.h"

#include <assert.h>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

char HeaderPhiPredictionSpeculation::ID = 0;
static RegisterPass< HeaderPhiPredictionSpeculation > rp("header-phi-pred-spec",
    "Check if header phi is a live-in under speculative assumptions", false, false);

HeaderPhiPredictionSpeculation::~HeaderPhiPredictionSpeculation()
{
}

void HeaderPhiPredictionSpeculation::getAnalysisUsage(AnalysisUsage &au) const
{
  //au.addRequired< DataLayout >();
  au.addRequired< TargetLibraryInfoWrapperPass >();
  au.addRequired< LoopAA >();
  au.addRequired< LAMPLoadProfile >();
  au.addRequired< SLAMPLoadProfile >();
  //au.addRequired< SmtxSpeculationManager >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< ModuleLoops >();
  au.addRequired< HeaderPhiLoadProfile >();
  au.setPreservesAll();
}

HeaderPhiPredictionSpeculation::pair_iterator HeaderPhiPredictionSpeculation::pair_begin(const Loop *loop) const
{
  return predicted_pairs.lower_bound( loop->getHeader() );
}

HeaderPhiPredictionSpeculation::pair_iterator HeaderPhiPredictionSpeculation::pair_end(const Loop *loop) const
{
  return predicted_pairs.upper_bound( loop->getHeader() );
}

HeaderPhiPredictionSpeculation::phi_iterator HeaderPhiPredictionSpeculation::phi_begin(const Loop *loop) const
{
  return predicted_phis.lower_bound( loop->getHeader() );
}

HeaderPhiPredictionSpeculation::phi_iterator HeaderPhiPredictionSpeculation::phi_end(const Loop *loop) const
{
  return predicted_phis.upper_bound( loop->getHeader() );
}

HeaderPhiPredictionSpeculation::phi_iterator HeaderPhiPredictionSpeculation::spec_phi_begin(const Loop *loop) const
{
  return speculated_phis.lower_bound( loop->getHeader() );
}

HeaderPhiPredictionSpeculation::phi_iterator HeaderPhiPredictionSpeculation::spec_phi_end(const Loop *loop) const
{
  return speculated_phis.upper_bound( loop->getHeader() );
}

bool HeaderPhiPredictionSpeculation::runOnModule(Module& m)
{
  //this->td = &getAnalysis< DataLayout >();
  this->td = &m.getDataLayout();
  this->mloops = &getAnalysis< ModuleLoops >();

  // build AA stack

  buildSpeculativeAnalysisStack();

  this->aa = getAnalysis< LoopAA >().getTopAA();

  return false;
}

bool HeaderPhiPredictionSpeculation::isPredictable(const Instruction *inst, const Loop *l)
{
  if( !l )
    return false;

  Loop* loop = const_cast<Loop*>(l);

  const PHINode* headerphi = dyn_cast< PHINode >(inst);
  if( !headerphi)
    return false;

  if (headerphi->getParent() != loop->getHeader())
    return false;

  // use profiling result

  HeaderPhiLoadProfile& profload = getAnalysis< HeaderPhiLoadProfile >();

  BasicBlock* preheader = loop->getLoopPreheader();
  assert(preheader && "loop->getLoopPreheader() == NULL. loop-simplify?\n");

  BasicBlock* latch = loop->getLoopLatch();
  assert(latch && "loop->getLoopLatch() == NULL. loop-simplify?\n");

  Instruction* fromlatch = dyn_cast<Instruction>( headerphi->getIncomingValueForBlock(latch) );
  if (!fromlatch)
    return profload.isPredictable( (uint64_t)Namer::getInstrId(headerphi) );

  this->ctrlspec->setLoopOfInterest( loop->getHeader() );

  Function*      f = latch->getParent();
  DominatorTree& dt = this->mloops->getAnalysis_DominatorTree(f);
  LoopInfo&      loopinfo = this->mloops->getAnalysis_LoopInfo(f);
  int            offset = 0;

  while (fromlatch)
  {
    if (fromlatch == headerphi)
    {
      // offset == 0 means that fromlatch is eventually same as the original value of headerphi

      if (offset == 0)
      {
        predicted_phis.insert( std::make_pair( l->getHeader(), const_cast<PHINode*>(headerphi) ) );
        return true;
      }
      else
      {
        bool ret = profload.isPredictable( (uint64_t)Namer::getInstrId(headerphi) );
        if (ret)
          speculated_phis.insert( std::make_pair( l->getHeader(), const_cast<PHINode*>(headerphi) ) );
        return ret;
      }
    }

    if (CastInst* ci = dyn_cast<CastInst>(fromlatch))
    {
      fromlatch = dyn_cast<Instruction>(ci->getOperand(0));
    }
    else if (LoadInst* li = dyn_cast<LoadInst>(fromlatch))
    {
      // count the number of store instructions within a loop which may alias with the load

      std::vector<Instruction*> srcs;
      this->collectDefs(li, loop, srcs);

      if ( srcs.size() == 0 )
      {
        // live-in feeds phi-node. This should not prevent parallelization, but should be confirmed
        // at runtime

        if (offset == 0)
        {
          // TODO: there is a room for improvement - offset doesn't need to be 0 here, if apply
          // speculation retrives "loaded value + offset" and feeds that value to the phi

          PhiLoadPair p(const_cast<PHINode*>(headerphi), li);
          predicted_pairs.insert( std::make_pair( l->getHeader(), p ) );
          return true;
        }
      }
      else if ( srcs.size() == 1)
      {
        if ( dt.dominates(srcs[0], fromlatch) ) // NOTE: might be too strong
          fromlatch = srcs[0];
      }
      else
      {
        bool ret = profload.isPredictable( (uint64_t)Namer::getInstrId(headerphi) );
        if (ret)
          speculated_phis.insert( std::make_pair( l->getHeader(), const_cast<PHINode*>(headerphi) ) );
        return ret;
      }
    }
    else if (StoreInst* si = dyn_cast<StoreInst>(fromlatch))
    {
      fromlatch = dyn_cast<Instruction>(si->getValueOperand());
    }
    else if (GetElementPtrInst* gi = dyn_cast<GetElementPtrInst>(fromlatch))
    {
      offset += computeOffset(gi, this->td);
      fromlatch = dyn_cast<Instruction>(gi->getPointerOperand());
    }
    else if (PHINode* phi = dyn_cast<PHINode>(fromlatch))
    {
      if ( loopinfo.isLoopHeader(phi->getParent()) )
      {
        bool ret = profload.isPredictable( (uint64_t)Namer::getInstrId(headerphi) );
        if (ret)
          speculated_phis.insert( std::make_pair( l->getHeader(), const_cast<PHINode*>(headerphi) ) );
        return ret;
      }

      // check if there is a biased incoming value

      fromlatch = this->getBiasedIncoming(phi);
    }
    else
    {
      bool ret = profload.isPredictable( (uint64_t)Namer::getInstrId(headerphi) );
      if (ret)
        speculated_phis.insert( std::make_pair( l->getHeader(), const_cast<PHINode*>(headerphi) ) );
      return ret;
    }
  }

  bool ret = profload.isPredictable( (uint64_t)Namer::getInstrId(headerphi) );
  if (ret)
    speculated_phis.insert( std::make_pair( l->getHeader(), const_cast<PHINode*>(headerphi) ) );
  return ret;
}

void HeaderPhiPredictionSpeculation::buildSpeculativeAnalysisStack()
{
  this->ctrlspec = &getAnalysis< ProfileGuidedControlSpeculator >();

  // Control Speculation
  this->ctrlspec->setLoopOfInterest(0);
}

void HeaderPhiPredictionSpeculation::collectDefs(LoadInst* li,
    Loop* loop,
    std::vector<Instruction*>& srcs)
{
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      if (queryIntraIterationMemoryDep(&*j, li, loop) || queryIntraIterationMemoryDep(&*j, li, loop))
        srcs.push_back(&*j);
    }
  }
}

Instruction* HeaderPhiPredictionSpeculation::getBiasedIncoming(PHINode* phi)
{
  Instruction* ret = NULL;

  for (unsigned i = 0 ; i < phi->getNumIncomingValues() ; i++)
  {
    if ( this->ctrlspec->phiUseIsSpeculativelyDead(phi, i) )
      continue;

    Instruction* incoming = dyn_cast<Instruction>(phi->getIncomingValue(i));

    if (ret && ret != incoming)
      return NULL;

    ret = incoming;
  }

  return ret;
}

bool HeaderPhiPredictionSpeculation::queryIntraIterationMemoryDep(Instruction* sop,
    Instruction* dop,
    Loop* loop)
{
  BasicBlock* header = loop->getHeader();
  std::pair<Instruction*, Instruction*> p(sop, dop);
  if ( cache[header].find(p) != cache[header].end() )
    return cache[header][p];

  bool maybeDep = false;
  if( ctrlspec->isReachable(sop,dop,loop) )
    maybeDep = queryMemoryDep(sop,dop,LoopAA::Same,LoopAA::Same,loop);

  cache[header][p] = maybeDep;
  return maybeDep;
}

bool HeaderPhiPredictionSpeculation::queryLoopCarriedMemoryDep(Instruction* sop,
    Instruction* dop,
    Loop* loop)
{
  BasicBlock* header = loop->getHeader();
  std::pair<Instruction*, Instruction*> p(sop, dop);
  if ( cache[header].find(p) != cache[header].end() )
    return cache[header][p];

  const bool maybeDep = queryMemoryDep(sop,dop,LoopAA::Before,LoopAA::After,loop);

  cache[header][p] = maybeDep;
  return maybeDep;
}

bool HeaderPhiPredictionSpeculation::queryMemoryDep(Instruction *sop, Instruction *dop,
    LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV, Loop* loop)
{
  if( ! sop->mayReadOrWriteMemory() )
    return false;
  if( ! dop->mayReadOrWriteMemory() )
    return false;
  if( ! sop->mayWriteToMemory() && ! dop->mayWriteToMemory() )
    return false;

  // forward dep test
  const LoopAA::ModRefResult forward = query(sop, FW, dop, loop);
  if( LoopAA::NoModRef == forward )
  {
    return false;
  }

  // Mod, ModRef, or Ref

  LoopAA::ModRefResult reverse = forward;
  if( FW != RV || sop != dop )
  {
    // reverse dep test
    reverse = query(dop, RV, sop, loop);

    if( LoopAA::NoModRef == reverse )
    {
      return false;
    }
  }

  if( LoopAA::Ref == forward && LoopAA::Ref == reverse )
  {
    return false; // RaR dep; who cares.
  }

  // Which result does the caller want?
  if(FW != RV)
  {
    if( forward == LoopAA::Mod || forward == LoopAA::ModRef )   // from Write
      if( reverse == LoopAA::Ref || reverse == LoopAA::ModRef ) // to Read
        return true;
    return false;
  }

  // ignore intra-iteration anti dependence
  // if there is a flow dependence on reverse case.
  if(FW == RV)
  {
    if( forward == LoopAA::Ref )   // from Read
      if( reverse == LoopAA::Mod || reverse == LoopAA::ModRef ) // to Write
        return false;
  }

  return true;
}

LoopAA::ModRefResult HeaderPhiPredictionSpeculation::query(Instruction *sop,
    LoopAA::TemporalRelation rel,
    Instruction *dop,
    Loop *loop)
{
  const LoopAA::ModRefResult res = this->aa->modref(sop,rel,dop,loop);
  return res;
}

}
}
