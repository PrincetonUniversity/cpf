#define DEBUG_TYPE "ctrlspec"

#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ControlSpecIterators.h"

namespace liberty
{
using namespace llvm;


// --------------------- BB Succ Iterator ----------------------

// Construct a begin iterator
BBSuccIterator::BBSuccIterator(TerminatorInst *ti, ControlSpeculation &cs)
  : ctrlspec(&cs), term(ti), sn(0)
{
  skipDead();
}

// Construnct an end iterator
BBSuccIterator::BBSuccIterator(TerminatorInst *ti)
  : ctrlspec(0), term(ti), sn( term->getNumSuccessors() )
{}

BasicBlock * BBSuccIterator::operator*() const
{
  assert( sn < term->getNumSuccessors() );
  return term->getSuccessor(sn);
}

BasicBlock * BBSuccIterator::operator->() const
{
  return this->operator*();
}

BBSuccIterator & BBSuccIterator::operator++()
{
  ++sn;
  skipDead();

  return *this;
}

void BBSuccIterator::skipDead()
{
  const unsigned N = term->getNumSuccessors();
  while( sn < N && ctrlspec->isSpeculativelyDead(term,sn) )
    ++sn;
}

bool BBSuccIterator::operator!=(const BBSuccIterator &other) const { return !this->operator==(other); }
bool BBSuccIterator::operator==(const BBSuccIterator &other) const
{
  return this->term == other.term
  &&     this->sn   == other.sn;
}


// --------------------- BB Pred Iterator ----------------------

BBPredIterator::BBPredIterator(BasicBlock *bb, ControlSpeculation &cs)
  : ctrlspec(&cs), succ(bb), piter( pred_begin(bb) )
{
  skipDead();
}

BBPredIterator::BBPredIterator(BasicBlock *bb)
  : ctrlspec(0), succ(bb), piter( pred_end(bb) )
{}

BasicBlock *BBPredIterator::operator*() const
{
  return *piter;
}

BasicBlock *BBPredIterator::operator->() const
{
  return this->operator*();
}

BBPredIterator & BBPredIterator::operator++()
{
  ++piter;
  skipDead();

  return *this;
}

void BBPredIterator::skipDead()
{
  const pred_iterator end = pred_end(succ);
  while( piter != end && ctrlspec->isSpeculativelyDead(*piter,succ) )
    ++piter;
}

bool BBPredIterator::operator!=(const BBPredIterator &other) const { return !this->operator==(other); }
bool BBPredIterator::operator==(const BBPredIterator &other) const
{
  return this->piter == other.piter;
}


// --------------------- Loop BB Succ Iterator ----------------------

// Three cases:
//  - Successors of ControlSpeculator::BeforeIteration
//    - There is exactly one: the loop header.
//  - Successors of a basic block
//    - Successors in the CFG which are not dead, backedges or loop exits.
//  - Successors of ControlSpeculator::AfterIteration
//    - There are no successors

// Construct a begin iterator
LoopBBSuccIterator::LoopBBSuccIterator(Loop *l, ControlSpeculation::LoopBlock lb, ControlSpeculation &cs)
  : loop(l), ctrlspec(&cs), pred(lb), sn(0)
{
  if( pred.isBeforeIteration() )
  {
    // There is exactly one successor: the loop header
  }
  else if( pred.isAfterIteration() )
  {
    // There are no successors
  }
  else
  {
    // Successors in the CFG which
    //  are not dead
    //  are not loop backedges
    //  are not loop exits.
    skipDead();
  }
}

// Construnct an end iterator
LoopBBSuccIterator::LoopBBSuccIterator(Loop *l, ControlSpeculation::LoopBlock lb)
  : loop(l), ctrlspec(0), pred(lb)
{
  if( pred.isBeforeIteration() )
  {
    sn = 1;
  }
  else if( pred.isAfterIteration() )
  {
    sn = 0;
  }
  else
  {
    assert( loop->contains( lb.getBlock() ) && "BB is not in the loop!" );
    sn = pred.getBlock()->getTerminator()->getNumSuccessors();
  }
}

ControlSpeculation::LoopBlock LoopBBSuccIterator::operator*() const
{
  if( pred.isBeforeIteration() )
  {
    assert( sn == 0 && "BeforeIteration has exactly ONE successor" );
    return ControlSpeculation::LoopBlock( loop->getHeader() );
  }
  else if( pred.isAfterIteration() )
  {
    assert( false && "AfterIteration has NO successors" );
  }
  else
  {
    TerminatorInst *term = pred.getBlock()->getTerminator();
    assert( sn < term->getNumSuccessors() && "Advance too far" );

    BasicBlock *succ = term->getSuccessor(sn);
    if( succ == loop->getHeader() )
      return ControlSpeculation::LoopBlock::LoopContinue();

    else if( ! loop->contains(succ) )
      return ControlSpeculation::LoopBlock::LoopExit( succ );

    else
      // Stay within one iteration.
      return ControlSpeculation::LoopBlock( succ );
  }
}

ControlSpeculation::LoopBlock LoopBBSuccIterator::operator->() const
{
  return this->operator*();
}

LoopBBSuccIterator & LoopBBSuccIterator::operator++()
{
  ++sn;
  skipDead();

  return *this;
}

void LoopBBSuccIterator::skipDead()
{
  if( ! pred.isBeforeIteration()
  &&  ! pred.isAfterIteration() )
  {
    TerminatorInst *term = pred.getBlock()->getTerminator();

    const unsigned N = term->getNumSuccessors();
    while( sn < N && ctrlspec->isSpeculativelyDead(term,sn) )
      ++sn;
  }
}

bool LoopBBSuccIterator::operator!=(const LoopBBSuccIterator &other) const { return !this->operator==(other); }
bool LoopBBSuccIterator::operator==(const LoopBBSuccIterator &other) const
{
  return this->pred == other.pred
  &&     this->loop == other.loop
  &&     this->sn   == other.sn;
}


// --------------------- Loop BB Pred Iterator ----------------------

// Three cases:
//  - Predecessors of ControlSpeculation::BeforeIteration
//  - Predecessors of a basic block
//  - Predecessors of ControlSpeculation::AfterIteration
//    - All loop exits or backedges.


LoopBBPredIterator::LoopBBPredIterator(Loop *l, ControlSpeculation::LoopBlock lb, ControlSpeculation &cs)
  : loop(l), ctrlspec(&cs), succ(lb), pn(0), list()
{
  BasicBlock *header = loop->getHeader();

  if( succ.isBeforeIteration() )
  {
    // There are no predecessors
  }
  else if( succ.isLoopContinue() )
  {
    // Loop backedges
    for(pred_iterator i=pred_begin(header), e=pred_end(header); i!=e; ++i)
    {
      BasicBlock *before_header = *i;
      if( ! loop->contains( before_header ) )
        continue;
      if( ctrlspec->isSpeculativelyDead(before_header,header) )
        continue;

      list.push_back( ControlSpeculation::LoopBlock( before_header ) );
    }
  }
  else if( succ.isLoopExit() )
  {
    // Loop exits.
    BasicBlock *exit = lb.getBlock();
    for(pred_iterator i=pred_begin(exit), e=pred_end(exit); i!=e; ++i)
    {
      BasicBlock *before_exit = *i;
      if( !loop->contains( before_exit ) )
        continue;

      list.push_back( ControlSpeculation::LoopBlock( before_exit ) );
    }
  }
  else
  {
    assert( loop->contains( lb.getBlock() ) && "BB is not in the loop!" );

    // Predecessors in the CFG which
    //  are not dead
    //  are not loop backedges
    //  are not loop exits.

    if( succ.getBlock() == header )
    {
      list.push_back( ControlSpeculation::LoopBlock::BeforeIteration() );
    }
    else
    {
      BasicBlock *succb = succ.getBlock();
      for(pred_iterator i=pred_begin(succb), e=pred_end(succb); i!=e; ++i)
      {
        BasicBlock *pred = *i;
        if( !ctrlspec->isSpeculativelyDead(pred,succb) )
          list.push_back( ControlSpeculation::LoopBlock( pred ) );
      }
    }
  }
}

LoopBBPredIterator::LoopBBPredIterator(Loop *l, ControlSpeculation::LoopBlock lb)
  : loop(l), ctrlspec(0), succ(lb), pn(0), list()
{
  // Note that list.size() == 0.
  // ==> isEndIterator().
}

bool LoopBBPredIterator::isEndIterator() const
{
  return pn >= list.size();
}

ControlSpeculation::LoopBlock LoopBBPredIterator::operator*() const
{
  assert( !succ.isBeforeIteration()
  && "BeforeIteration has NO predecessors");

  return list[pn];
}

ControlSpeculation::LoopBlock LoopBBPredIterator::operator->() const
{
  return this->operator*();
}

LoopBBPredIterator & LoopBBPredIterator::operator++()
{
  ++pn;
  return *this;
}

bool LoopBBPredIterator::operator!=(const LoopBBPredIterator &other) const { return !this->operator==(other); }
bool LoopBBPredIterator::operator==(const LoopBBPredIterator &other) const
{
  return this->succ == other.succ
  &&     this->loop == other.loop
  && (  (!this->isEndIterator() && !other.isEndIterator() && this->pn == other.pn)
     || ( this->isEndIterator() &&  other.isEndIterator() )
     );
}


}


