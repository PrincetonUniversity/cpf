#define DEBUG_TYPE "pipeline"

#include "liberty/Analysis/ControlSpecIterators.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ReductionDetection.h"
#include "liberty/Orchestration/PredictionSpeculation.h"
#include "liberty/Speculation/LoopDominators.h"

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include "Exp.h"
#include "Exp_PDG_NoTiming.h"

#include <sys/time.h>

namespace liberty
{
namespace SpecPriv
{
namespace FastDagSccExperiment
{
using namespace llvm;

Exp_PDG_NoTiming::Exp_PDG_NoTiming(const Vertices &v, ControlSpeculation &cs, PredictionSpeculation &predspec, const DataLayout *td, bool ignore)
: numQueries(0), numNoModRefQueries(0), numDepQueries(0), numPositiveDepQueries(0), numQueriesSavedBecauseRedundantRegCtrl(0),
  V(v), E(V), ctrlspec(cs), ignoreAntiOutput(ignore)
{
  computeRegisterDeps(predspec);
  computeControlDeps(ctrlspec, td);
}

Exp_PDG_NoTiming::~Exp_PDG_NoTiming()
{
  DEBUG(pstats( errs() ));
}

void Exp_PDG_NoTiming::pstats(raw_ostream &fout) const
{
/*
  fout << "PDG issued " << numQueries << " memory queries.\n";

  unsigned numMemOps = 0;
  for(unsigned i=0; i<V.size(); ++i)
  {
    Instruction *inst = V.get(i);
    if( inst->mayReadOrWriteMemory() )
      ++numMemOps;
  }

  const unsigned eachToEach = numMemOps * numMemOps;
  const unsigned queriesPerDep = 2;
  const unsigned nII = queriesPerDep * eachToEach;
  const unsigned nLC = queriesPerDep * eachToEach;
  const unsigned worstCase = nII + nLC;

  fout << "  This is " << (100 * numQueries / worstCase) << "% of " << worstCase << " worst case.\n";

  getE().pstats(fout);
*/
}

void Exp_PDG_NoTiming::computeRegisterDeps(PredictionSpeculation &predspec)
{
  Loop *loop = V.getLoop();

  // Add register .
  for(Vertices::ID u=0, N=V.size(); u<N; ++u)
  {
    const Instruction *user = V.get(u);
    const bool loopCarried = (user->getParent() == loop->getHeader() && isa<PHINode>(user));
    const bool predictable = predspec.isPredictable(user,loop);

    // For each operand of user which is also an instruction in the loop
    for(User::const_op_iterator j=user->op_begin(), z=user->op_end(); j!=z; ++j)
    {
      const Value *operand = *j;
      if( const Instruction *src = dyn_cast<Instruction>(operand) )
        if( V.count(src) )
        {
          Vertices::ID s = V.get(src);
          if( loopCarried )
          {
            // redux remediator will resolve these reg deps on demand
            /*
	          if ( UseRedux )
            {
              RecurrenceDescriptor RedDes;
              ReductionDetection reduxdet;
              Instruction *I = const_cast<Instruction *>(user);
              auto *Phi = dyn_cast<PHINode>(I);

              if ( (loop->getLoopPreheader() && RecurrenceDescriptor::isReductionPHI(Phi, loop, RedDes)) )
                continue;

              if ( reduxdet.isSumReduction(loop, src, user, loopCarried) )
                continue;

              if ( reduxdet.isMinMaxReduction(loop, src, user, loopCarried) )
                continue;
            }
            */
            if( !predictable )
              E.addLCReg(s,u);
          }
          else
            E.addIIReg(s,u);
        }
    }
  }
}

void Exp_PDG_NoTiming::computeControlDeps(ControlSpeculation &ctrlspec, const DataLayout *td)
{
  Loop *loop = V.getLoop();

  // Add intra-iteration control dependences.
  LoopPostDom pdt(ctrlspec, loop);
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock dst = ControlSpeculation::LoopBlock( *i );
    for(LoopPostDom::pdf_iterator j=pdt.pdf_begin(dst), z=pdt.pdf_end(dst); j!=z; ++j)
    {
      ControlSpeculation::LoopBlock src = *j;

      TerminatorInst *term = src.getBlock()->getTerminator();
      assert( !ctrlspec.isSpeculativelyUnconditional(term)
      && "Unconditional branches do not source control deps (ii)");

      Vertices::ID t = V.get(term);
      for(BasicBlock::iterator k=dst.getBlock()->begin(), f=dst.getBlock()->end(); k!=f; ++k)
      {
        Instruction *idst = &*k;

        /*
        // Draw ctrl deps to:
        //  (1) Operations with side-effects
        //  (2) Conditional branches.
        if( isSafeToSpeculativelyExecute(idst,td) )
          continue;
        */

        Vertices::ID s = V.get( idst );
        E.addIICtrl(t, s);
      }
    }
  }

  // TODO: ideally, a PHI dependence is drawn from
  // a conditional branch to a PHI node iff the branch
  // controls which incoming value is selected by that PHI.

  // That's a pain to compute.  Instead, we will draw a
  // dependence from branches to PHIs in successors.
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();
    Vertices::ID t = V.get(term);

    // no control dependence can be formulated around unconditional branches

    if (ctrlspec.isSpeculativelyUnconditional(term))
      continue;


    for(ControlSpeculation::succ_iterator j=ctrlspec.succ_begin(bb), z=ctrlspec.succ_end(bb); j!=z; ++j)
    {
      BasicBlock *succ = *j;
      if( !loop->contains(succ) )
        continue;

      const bool loop_carried = (succ == loop->getHeader());

      for(BasicBlock::iterator k=succ->begin(); k!=succ->end(); ++k)
      {
        PHINode *phi = dyn_cast<PHINode>(&*k);
        if( !phi )
          break;
        if( phi->getNumIncomingValues() == 1 )
          continue;
        Vertices::ID p = V.get(phi);

        if( loop_carried )
          E.addLCCtrl(t,p);
        else
          E.addIICtrl(t,p);
      }
    }
  }

  // Add loop-carried control dependences.
  // Foreach loop-exit.
  typedef ControlSpeculation::ExitingBlocks Exitings;

  Exitings exitings;
  ctrlspec.getExitingBlocks(loop, exitings);
  for(Exitings::iterator i=exitings.begin(), e=exitings.end(); i!=e; ++i)
  {
    BasicBlock *exiting = *i;
    TerminatorInst *term = exiting->getTerminator();
    assert( !ctrlspec.isSpeculativelyUnconditional(term)
    && "Unconditional branches do not source control deps (lc)");

    Vertices::ID t = V.get(term);

    // Draw ctrl deps to:
    //  (1) Operations with side-effects
    //  (2) Loop exits.
    for(Loop::block_iterator j=loop->block_begin(), z=loop->block_end(); j!=z; ++j)
    {
      BasicBlock *dst = *j;
      for(BasicBlock::iterator k=dst->begin(), g=dst->end(); k!=g; ++k)
      {
        Instruction *idst = &*k;

        /*
        // Draw ctrl deps to:
        //  (1) Operations with side-effects
        //  (2) Loop exits
        if( isSafeToSpeculativelyExecute(idst,td) )
          continue;
        */

        if( TerminatorInst *tt = dyn_cast< TerminatorInst >(idst) )
          if( ! ctrlspec.mayExit(tt,loop) )
            continue;

        Vertices::ID s = V.get( idst );
        E.addLCCtrl(t, s);
      }
    }
  }
}

bool Exp_PDG_NoTiming::queryMemoryDep(Vertices::ID src, Vertices::ID dst, LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV)
{
  Instruction *sop = V.get(src), *dop = V.get(dst);

  return queryMemoryDep(sop,dop,FW,RV);
}

static LoopAA::ModRefResult join(const LoopAA::ModRefResult a, const LoopAA::ModRefResult b)
{
  return (LoopAA::ModRefResult) (a|b);
}

LoopAA::ModRefResult Exp_PDG_NoTiming::query(Instruction *sop, LoopAA::TemporalRelation rel, Instruction *dop, Loop *loop)
{
//  struct timeval start, stop;

  ++numQueries;

  Remedies R;

//  gettimeofday(&start,0);
  LoopAA::ModRefResult res;
  if( HideContext )
  {
    // Intentionally mix loop-carried, intra-iteration, and loop-insensitive queries
    // to create a context-blind result.

    res = aa->modref(sop,LoopAA::Same,dop,0,R); // Probably the least precise of these four.
    if( res != LoopAA::ModRef ) // Don't waste time if already worst-case
    {
      const LoopAA::ModRefResult res2 = aa->modref(sop,LoopAA::After,dop,loop,R);
      res = join(res, res2); // can only get worse

      if( res != LoopAA::ModRef ) // Don't waste time if already worst-case
      {
        const LoopAA::ModRefResult res3 = aa->modref(sop,LoopAA::Same,dop,loop,R);
        res = join(res, res3); // can only get worse

        if( res != LoopAA::ModRef ) // Don't waste time if already worst-case
        {
          const LoopAA::ModRefResult res4 = aa->modref(sop,LoopAA::Before,dop,loop,R);
          res = join(res, res4); // can only get worse
        }
      }
    }
  }
  else
  {
    // Normal query, context not hidden.
    res = aa->modref(sop,rel,dop,loop,R);
  }

//  gettimeofday(&stop,0);

  if( LoopAA::NoModRef == res )
    ++numNoModRefQueries;

//  const uint64_t microseconds = 1e6*(stop.tv_sec - start.tv_sec) + (stop.tv_usec - start.tv_usec);
//  totalAATime += microseconds;

  return res;
}

bool Exp_PDG_NoTiming::queryMemoryDep(Instruction *sop, Instruction *dop, LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV)
{
  if( ! sop->mayReadOrWriteMemory() )
    return false;
  if( ! dop->mayReadOrWriteMemory() )
    return false;
  if( ! sop->mayWriteToMemory() && ! dop->mayWriteToMemory() )
    return false;

  ++numDepQueries;

  Loop *loop = V.getLoop();

  // forward dep test
  const LoopAA::ModRefResult forward = query(sop, FW, dop, loop);
  if( LoopAA::NoModRef == forward )
    return false;

  // Mod, ModRef, or Ref

  LoopAA::ModRefResult reverse = forward;
  if( FW != RV || sop != dop )
  {
    // reverse dep test
    reverse = query(dop, RV, sop, loop);

    if( LoopAA::NoModRef == reverse )
      return false;
  }

  if( LoopAA::Ref == forward && LoopAA::Ref == reverse )
    return false; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.

  // Which result does the caller want?
  if( ignoreAntiOutput && FW != RV )
  {
    if( forward == LoopAA::Mod || forward == LoopAA::ModRef )   // from Write
      if( reverse == LoopAA::Ref || reverse == LoopAA::ModRef ) // to Read
      {
        ++numPositiveDepQueries;
        return true;
      }

    return false;
  }

  ++numPositiveDepQueries;
  return true;
}

bool Exp_PDG_NoTiming::queryIntraIterationMemoryDep(Vertices::ID src, Vertices::ID dst, bool force)
{
  if( !force && E.hasIntraIterationEdge(src,dst) )
  {
    const PartialEdgeSet &cpes = E;
    const PartialEdge &edge = cpes.find(src,dst);
    if( edge.ii_reg || edge.ii_ctrl )
      ++numQueriesSavedBecauseRedundantRegCtrl;
    return true;
  }

  if( E.knownIntraIterationEdge(src,dst) )
    return E.hasIntraIterationEdge(src,dst);

  Loop *loop = V.getLoop();

  bool maybeDep = false;

  Instruction *sop = V.get(src), *dop = V.get(dst);
  if( ctrlspec.isReachable(sop,dop,loop) )
    maybeDep = queryMemoryDep(sop,dop, LoopAA::Same,LoopAA::Same);

  E.addIIMem(src,dst, maybeDep);
  return maybeDep;
}

bool Exp_PDG_NoTiming::queryLoopCarriedMemoryDep(Vertices::ID src, Vertices::ID dst, bool force)
{
  if( !force && E.hasLoopCarriedEdge(src,dst) )
  {
    const PartialEdgeSet &cpes = E;
    const PartialEdge &edge = cpes.find(src,dst);
    if( edge.lc_reg || edge.lc_ctrl )
      ++numQueriesSavedBecauseRedundantRegCtrl;
    return true;
  }

  if( E.knownLoopCarriedEdge(src,dst) )
    return E.hasLoopCarriedEdge(src,dst);

  const bool maybeDep = queryMemoryDep(src,dst, LoopAA::Before,LoopAA::After);

  E.addLCMem(src,dst, maybeDep);
  return maybeDep;
}

bool Exp_PDG_NoTiming::hasEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasEdge(src,dst);
}

bool Exp_PDG_NoTiming::hasLoopCarriedEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasLoopCarriedEdge(src,dst);
}

bool Exp_PDG_NoTiming::unknown(Vertices::ID src, Vertices::ID dst) const
{
  return !E.knownLoopCarriedEdge(src,dst)
    &&   !E.knownIntraIterationEdge(src,dst);
}

bool Exp_PDG_NoTiming::unknownLoopCarried(Vertices::ID src, Vertices::ID dst) const
{
  return !E.knownLoopCarriedEdge(src,dst);
}

bool Exp_PDG_NoTiming::queryIntraIterationMemoryDep_OnlyCountNumQueries(Vertices::ID src, Vertices::ID dst, bool force)
{
  const bool hasEdge = E.hasIntraIterationEdge(src,dst);
  if( (!force && hasEdge) || E.knownIntraIterationEdge(src,dst) )
    return hasEdge;

  Loop *loop = V.getLoop();

  bool maybeDep = false;

  Instruction *sop = V.get(src), *dop = V.get(dst);
  if( ctrlspec.isReachable(sop,dop,loop) )
    maybeDep = queryMemoryDep_OnlyCountNumQueries(sop,dop, LoopAA::Same,LoopAA::Same);

  E.addIIMem(src,dst, maybeDep);
  return maybeDep;
}

bool Exp_PDG_NoTiming::queryLoopCarriedMemoryDep_OnlyCountNumQueries(Vertices::ID src, Vertices::ID dst, bool force)
{
  const bool hasEdge = E.hasLoopCarriedEdge(src,dst);
  if( (!force && hasEdge) || E.knownLoopCarriedEdge(src,dst) )
    return hasEdge;

  const bool maybeDep = queryMemoryDep_OnlyCountNumQueries(src,dst, LoopAA::Before,LoopAA::After);

  E.addLCMem(src,dst, maybeDep);
  return maybeDep;
}

bool Exp_PDG_NoTiming::queryMemoryDep_OnlyCountNumQueries(Vertices::ID src, Vertices::ID dst, LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV)
{
  Instruction *sop = V.get(src), *dop = V.get(dst);

  return queryMemoryDep_OnlyCountNumQueries(sop,dop,FW,RV);
}



bool Exp_PDG_NoTiming::queryMemoryDep_OnlyCountNumQueries(Instruction *sop, Instruction *dop, LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV)
{
  if( ! sop->mayReadOrWriteMemory() )
    return false;
  if( ! dop->mayReadOrWriteMemory() )
    return false;
  if( ! sop->mayWriteToMemory() && ! dop->mayWriteToMemory() )
    return false;

  Loop *loop = V.getLoop();

  // forward dep test
  query_OnlyCountNumQueries(sop, FW, dop, loop);
  return true;
/*
  const LoopAA::ModRefResult forward = query_OnlyCountNumQueries(sop, FW, dop, loop);
  if( LoopAA::NoModRef == forward )
    return false;

  // Mod, ModRef, or Ref

  LoopAA::ModRefResult reverse = forward;
  if( FW != RV || sop != dop )
  {
    // reverse dep test
    reverse = query_OnlyCountNumQueries(dop, RV, sop, loop);

    if( LoopAA::NoModRef == reverse )
      return false;
  }

  if( LoopAA::Ref == forward && LoopAA::Ref == reverse )
    return false; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.

  // Which result does the caller want?
  if( ignoreAntiOutput && FW != RV )
  {
    if( forward == LoopAA::Mod || forward == LoopAA::ModRef )   // from Write
      if( reverse == LoopAA::Ref || reverse == LoopAA::ModRef ) // to Read
        return true;
    return false;
  }

  return true;
*/
}

LoopAA::ModRefResult Exp_PDG_NoTiming::query_OnlyCountNumQueries(Instruction *sop, LoopAA::TemporalRelation rel, Instruction *dop, Loop *loop)
{
  ++numQueries;
  return LoopAA::ModRef;
}

liberty::SpecPriv::PDG Exp_PDG_NoTiming::toNormalPDG()
{
  return liberty::SpecPriv::PDG(V,E,ctrlspec,ignoreAntiOutput,aa);
}

/*
bool Exp_PDG_NoTiming::tryRemoveLoopCarriedMemEdge(Vertices::ID src,
                                                      Vertices::ID dst) {
  ++numComplaints;

  Loop *loop = V.getLoop();
  Instruction *sop = V.get(src);
  Instruction *dop = V.get(dst);

  // forward dep test
  const LoopAA::ModRefResult forward =
      remed->modref(sop, LoopAA::Before, dop, loop);
  if (LoopAA::NoModRef == forward)
    return true;

  // Mod, ModRef, or Ref

  LoopAA::ModRefResult reverse = forward;
  if (sop != dop) {
    // reverse dep test
    reverse = remed->modref(dop, LoopAA::After, sop, loop);

    if (LoopAA::NoModRef == reverse)
      return true;
  }

  if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
    return true; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.

  // could check if client cares only about flow deps but for now consider all except for RAR deps as important

  return false;
}

bool Exp_PDG_NoTiming::tryRemoveLoopCarriedRegEdge(Vertices::ID src,
                                                   Vertices::ID dst) {
  ++numComplaints;

  Loop *loop = V.getLoop();
  Instruction *sop = V.get(src);
  Instruction *dop = V.get(dst);

  Remediator::RemedResp resp = remed->regdep(sop, dop, true, loop);
  //Remediator::DepResult res = remed->regdep(sop, dop, true, loop);

  //if ( res == Remediator::NoDep)
  if ( resp.result == Remediator::NoDep)
    return true;
  return false;
}

bool Exp_PDG_NoTiming::tryRemoveLoopCarriedCtrlEdge(Vertices::ID src,
                                                  Vertices::ID dst) {
  ++numComplaints;

  Loop *loop = V.getLoop();
  Instruction *sop = V.get(src);
  Instruction *dop = V.get(dst);

  Remediator::RemedResp resp = remed->ctrldep(sop, dop, loop);
  //Remediator::DepResult res = remed->ctrldep(sop, dop, loop);


  //if ( res == Remediator::NoDep)
  if ( resp.result == Remediator::NoDep)
    return true;
  return false;
}
*/

void Exp_PDG_NoTiming::removeLoopCarriedMemEdge(Vertices::ID src,
                                               Vertices::ID dst) {
  E.removeLCMem(src, dst);
}

void Exp_PDG_NoTiming::removeLoopCarriedCtrlEdge(Vertices::ID src,
                                                Vertices::ID dst) {
  E.removeLCCtrl(src, dst);
}

void Exp_PDG_NoTiming::removeLoopCarriedRegEdge(Vertices::ID src,
                                               Vertices::ID dst) {
  E.removeLCReg(src, dst);
}
}
}
}

