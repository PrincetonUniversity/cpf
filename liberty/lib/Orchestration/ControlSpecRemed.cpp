#define DEBUG_TYPE "ctrlspec-remed"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ControlSpecIterators.h"
#include "liberty/Orchestration/ControlSpecRemed.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Speculation/LoopDominators.h"
#include "liberty/Utilities/Timer.h"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEFAULT_CTRL_REMED_COST 50

namespace liberty
{
using namespace llvm;
using namespace SpecPriv;

STATISTIC(numQueries,          "Num mem queries in cntr spec remediator");
STATISTIC(numMemDepRem,        "Num removed mem dep with cntr spec remediator");
STATISTIC(numRegQueries,       "Num reg queries in cntr spec remediator");
STATISTIC(numCtrlQueries,      "Num ctrl queries in cntr spec remediator");
STATISTIC(numCtrlDepRem,       "Num removed ctrl dep with cntr spec remediator");
STATISTIC(numRegDepRem,        "Num removed reg dep with cntr spec remediator");

void ControlSpecRemedy::apply(Task *task) {
  // TODO: transfer the code for application of control spec here.
}

bool ControlSpecRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<ControlSpecRemedy> ctrlSpecRhs =
      std::static_pointer_cast<ControlSpecRemedy>(rhs);
  return this->brI < ctrlSpecRhs->brI;
}

// discover all the ctrl edges that cannot be speculated and populate
// the unremovableCtrlDeps set
// This code is almost identical to PDG::computeControlDeps
void ControlSpecRemediator::processLoopOfInterest(Loop *l) {
  loop = l;

  // clean up the unremovableCtrlDeps set from a previous loop
  unremovableCtrlDeps.clear();

  // build a partialEdgeSet that holds transitive II-ctrl dependence info
  //EdgeSet IICtrlCache;

  // Detect intra-iteration control dependences that cannot be speculated
  LoopPostDom pdt(*speculator, loop);

  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock dst = ControlSpeculation::LoopBlock( *i );
    // pdf only for intra-loop dominance, does not consider backedges
    for(LoopPostDom::pdf_iterator j=pdt.pdf_begin(dst), z=pdt.pdf_end(dst); j!=z; ++j)
    {
      ControlSpeculation::LoopBlock src = *j;

      TerminatorInst *term = src.getBlock()->getTerminator();
      assert( !speculator->isSpeculativelyUnconditional(term)
      && "Unconditional branches do not source control deps (ii)");

      //Vertices::ID t = V.get(term);
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

        //Vertices::ID s = V.get( idst );
        //E.addIICtrl(t, s);
        unremovableCtrlDeps[term].insert(idst);
        //IICtrlCache.addIICtrl(t, s);
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
    //Vertices::ID t = V.get(term);

    // no control dependence can be formulated around unconditional branches

    if (speculator->isSpeculativelyUnconditional(term))
      continue;

    for(ControlSpeculation::succ_iterator j=speculator->succ_begin(bb), z=speculator->succ_end(bb); j!=z; ++j)
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
        //Vertices::ID p = V.get(phi);

        unremovableCtrlDeps[term].insert(phi);
        //if( loop_carried )
          //E.addLCCtrl(t,p);
        //else
          //E.addIICtrl(t,p);
        //if ( !loop_carried )
        //  IICtrlCache.addIICtrl(t, p);

        //DEBUG(errs() << "Unremovable ctrl dep between term " << *term << " and phi " << *phi << '\n' );
      }
    }
  }

  // build a partialEdgeSet that holds transitive II-ctrl dependence info
  //buildTransitiveIntraIterationControlDependenceCache(IICtrlCache, V);

  // Add loop-carried control dependences.
  // Foreach loop-exit.
  typedef ControlSpeculation::ExitingBlocks Exitings;

  Exitings exitings;
  speculator->getExitingBlocks(loop, exitings);
  for(Exitings::iterator i=exitings.begin(), e=exitings.end(); i!=e; ++i)
  {
    BasicBlock *exiting = *i;
    TerminatorInst *term = exiting->getTerminator();
    assert( !speculator->isSpeculativelyUnconditional(term)
    && "Unconditional branches do not source control deps (lc)");

    //Vertices::ID t = V.get(term);

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

        /*
        if( TerminatorInst *tt = dyn_cast< TerminatorInst >(idst) )
          if( ! speculator->mayExit(tt,loop) )
            continue;
        */

        //Vertices::ID s = V.get( idst );

        // Draw LC ctrl dep only when there is no (transitive) II ctrl dep from t to s

        //if ( hasTransitiveIntraIterationControlDependence(t, s) )
        //if ( IICtrlCache.hasEdge(t, s) )
        //  continue;

        //E.addLCCtrl(t, s);
        unremovableCtrlDeps[term].insert(idst);
        //DEBUG(errs() << "Unremovable ctrl dep between term " << *term << " and idst " << *idst << '\n' );
      }
    }
  }
}

Remediator::RemedResp ControlSpecRemediator::memdep(const Instruction *A,
                                                    const Instruction *B,
                                                    bool LoopCarried, bool RAW,
                                                    const Loop *L) {

  ++numQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<ControlSpecRemedy> remedy =
      std::shared_ptr<ControlSpecRemedy>(new ControlSpecRemedy());
  remedy->cost = DEFAULT_CTRL_REMED_COST;

  // isReachable function call requires non-const parameters
  Loop *ncL = const_cast<Loop*>(L);
  Instruction *ncA = const_cast<Instruction*>(A);
  Instruction *ncB = const_cast<Instruction*>(B);

  if (speculator->isSpeculativelyDead(A)) {
    ++numMemDepRem;
    remedResp.depRes = DepResult::NoDep;
    DEBUG(errs() << "CtrlSpecRemed removed mem dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  else if (speculator->isSpeculativelyDead(B)) {
    ++numMemDepRem;
    remedResp.depRes = DepResult::NoDep;
    DEBUG(errs() << "CtrlSpecRemed removed mem dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  else if (!LoopCarried && speculator->isReachable(ncA, ncB, ncL) == false) {
    ++numMemDepRem;
    remedResp.depRes = DepResult::NoDep;
    DEBUG(errs() << "CtrlSpecRemed removed mem dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

// can a ctrl dep from A to B be removed?
Remediator::RemedResp ControlSpecRemediator::ctrldep(const Instruction *A,
                                                     const Instruction *B,
                                                     const Loop *L) {
  ++numCtrlQueries;

  assert(L == loop && "The ctrl dep query does not refer to the loop of "
                      "interest of the control spec remediator");

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<ControlSpecRemedy> remedy =
      std::shared_ptr<ControlSpecRemedy>(new ControlSpecRemedy());
  remedy->cost = DEFAULT_CTRL_REMED_COST;

  // check if the control speculator was able to remove the control
  // dependence when it preprocesed the loop

  if (unremovableCtrlDeps.count(A)) {
    auto &unremCtrlDepsFromA = unremovableCtrlDeps[A];
    if (unremCtrlDepsFromA.count(B)) {
      // unable to remove this ctrl dep
      remedResp.remedy = remedy;
      return remedResp;
    }
  }

  // ctrl dep is removable by control speculation
  ++numCtrlDepRem;
  remedy->brI = A;
  remedResp.depRes = DepResult::NoDep;
  DEBUG(errs() << "CtrlSpecRemed removed ctrl dep between inst " << *A
               << "  and  " << *B << '\n');

  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedResp ControlSpecRemediator::regdep(const Instruction *A,
                                                    const Instruction *B,
                                                    bool loopCarried,
                                                    const Loop *L) {
  ++numRegQueries;

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<ControlSpecRemedy> remedy =
      std::shared_ptr<ControlSpecRemedy>(new ControlSpecRemedy());
  remedy->cost = DEFAULT_CTRL_REMED_COST;

  // check if the inst that source the dependence is speculatively dead
  if (speculator->isSpeculativelyDead(A)) {
    ++numRegDepRem;
    remedResp.depRes = DepResult::NoDep;
    DEBUG(errs() << "CtrlSpecRemed removed reg dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  // check if the inst that sinks the dependence is speculatively dead
  else if (speculator->isSpeculativelyDead(B)) {
    ++numRegDepRem;
    remedResp.depRes = DepResult::NoDep;
    DEBUG(errs() << "CtrlSpecRemed removed reg dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  else {
    const PHINode *phi = dyn_cast<PHINode>(B);
    if (phi && speculator->phiUseIsSpeculativelyDead(phi, A)) {
      ++numRegDepRem;
      remedResp.depRes = DepResult::NoDep;
      DEBUG(errs() << "CtrlSpecRemed removed reg dep between inst " << *A
                   << "  and  " << *B << '\n');
    }
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
