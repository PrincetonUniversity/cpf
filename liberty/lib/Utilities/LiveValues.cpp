#define DEBUG_TYPE "live-values"

#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/MathExtras.h"

#include "liberty/Utilities/LiveValues.h"

namespace liberty
{

  using namespace llvm;

  typedef GraphTraits<const BasicBlock*> GB;
  typedef GB::ChildIteratorType GBI;
  typedef GraphTraits<Inverse<const BasicBlock*> > GR;
  typedef GR::ChildIteratorType GRI;

  LiveValues::LiveValues(
    const Function &fcn,
    bool includeFcnArgs)
  : numbers(), revNumbers(), OUT()
  {
    LLVM_LLVM_DEBUG(errs() << "\t- Performing dataflow analysis");

    // Now we are going to do some data flow analysis
    // in order to identify the live values at each
    // callsite.
    // We say that each use GENs a value, and that
    // each def KILLs a value.
    //
    // In order to efficiently operate on these
    // sets, we first assign unique, consecutive integers
    // to each value in this function, so that we may use
    // dense bit vectors to represent the sets.

    if( includeFcnArgs )
    {
      for(Function::const_arg_iterator i=fcn.arg_begin(), e=fcn.arg_end(); i!=e; ++i)
        assignValueNumber(&*i);
    }

    for(const_inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
      assignValueNumber(&*i);

    const unsigned num_blocks = NextPowerOf2( fcn.size() );
    const unsigned num_values = revNumbers.size();

    // Next, we will compute the GEN and KILL
    // sets for each basic block.
    ValueSets GEN(  num_blocks ),
              NOT_KILL( num_blocks );
    for(Function::const_iterator i=fcn.begin(), e=fcn.end(); i!=e; ++i)
    {
      const BasicBlock *bb = &*i;

      GEN[bb]. resize(num_values);
      BitVector KILL(num_values);

      // visit instructions in REVERSE;
      BasicBlock::const_iterator j=bb->end(), f=bb->begin();
      while( j != f )
      {
        --j;
        const Instruction *def = &*j;
        if( numbers.count(def) )
        {
          // Record the defs
          KILL.set( numbers[def] );
          GEN[bb].reset( numbers[def] );
        }

        // Record the uses
        // UNLESS it's a PHI node,
        // since PHIs only use the valus for some preds...
        if( !isa< PHINode >(def) )
        {
          typedef Instruction::const_op_iterator OpIt;
          for(OpIt k=def->op_begin(), g=def->op_end(); k!=g; ++k)
          {
            const Value *use = *k;
            if( ! numbers.count(use) )
              continue;

            GEN[bb].set( numbers[use] );
          }
        }
      }

      NOT_KILL[bb].swap(KILL);
      NOT_KILL[bb].flip();
    }

    // We will repeatedly apply the following
    // update:
    //  IN(bb) = UNION [succ bb] OUT
    //  OUT(bb) = IN(bb) - KILL(bb) + GEN(bb)

    // This is a BACKWARDS dataflow problem.
    // We will visit each block at least once, and
    // re-visit predecessors when OUT sets change

//    ValueSets OUT(num_blocks);
    typedef std::vector<const BasicBlock*> Fringe;
    Fringe fringe;
    for(Function::const_iterator i=fcn.begin(), e=fcn.end(); i!=e; ++i)
      fringe.push_back( &*i );

    BitVector newOUT(num_values);
    while( ! fringe.empty() )
    {
      LLVM_LLVM_DEBUG(errs() << '.');
      const BasicBlock *bb = fringe.back();
      fringe.pop_back();

      newOUT.reset();
      computeIN(bb, newOUT);
      newOUT &= NOT_KILL[bb];
      newOUT |= GEN[bb];

      if( OUT[bb] != newOUT )
      {
        for(GRI i=GR::child_begin(bb), e=GR::child_end(bb); i!=e; ++i)
          fringe.push_back(*i);

        OUT[bb].swap(newOUT);
      }
    }
    LLVM_LLVM_DEBUG(errs() << '\n');

/*
    errs() << "===================================================================\n\n\n";
    errs() << fcn.getName() << "\n\n";
    ValueList xxx;
    for(Function::const_iterator i=fcn.begin(), e=fcn.end(); i!=e; ++i)
    {
      const BasicBlock *bb = &*i;

      errs() << "Live-ins: ";
      xxx.clear();
      findLiveInToBB(bb,xxx);
      for(ValueList::iterator i=xxx.begin(), e=xxx.end(); i!=e; ++i)
        errs() << (*i)->getName() << ", ";
      errs() << '\n';

      errs() << *bb << '\n';

      errs() << "Live-outs: ";
      xxx.clear();
      findLiveOutFromBB(bb,xxx);
      for(ValueList::iterator i=xxx.begin(), e=xxx.end(); i!=e; ++i)
        errs() << (*i)->getName() << ", ";
      errs() << '\n';
    }
*/
  }

  void LiveValues::assignValueNumber(const Value *v)
  {
    // only bother for values with at least one use.
    // this makes our bit-vectors smaller.
    if( v->use_empty() )
      return;

    numbers[v] = revNumbers.size();
    revNumbers.push_back(v);
  }


  static void translateBitVectorToValues(
    const BitVector &bitset,
    const LiveValues::Num2Value &revNumbers,
    LiveValues::ValueList &valueset)
  {
    valueset.reserve( bitset.count() );

    // translate this bit vector into a set
    // of values.
    // For each true-bit in the bit vector
    // <==> for each live value in the set.
    for(int j=bitset.find_first(); j >= 0; j=bitset.find_next(j) )
    {
      const Value *lv = revNumbers[j];

      // sanity: ensure this is a definition
      if( isa<StoreInst>(lv) )
        continue;
      if( isa<TerminatorInst>(lv) )
        if( !isa<InvokeInst>(lv) )
          continue;
      if( lv->getType()->isVoidTy() )
        continue;

      valueset.push_back( lv );
    }
  }

  void LiveValues::findLiveInToBB(
    const BasicBlock *bb,
    ValueList &liveIns) const
  {
    ValueSets::const_iterator i = OUT.find(bb);
    if( i == OUT.end() )
      return;

    BitVector liveins( i->second );
    liveins.resize( revNumbers.size() );

    // Be conservative, since the query
    // doesn't provide specific info.
    if( const PHINode *phi = dyn_cast< PHINode >( &bb->front() ) )
      for(unsigned j=0, N=phi->getNumIncomingValues(); j<N; ++j)
      {
        const BasicBlock *pred = phi->getIncomingBlock(j);
        addUsesFromPHI(pred, bb, liveins);
      }

    translateBitVectorToValues(liveins, revNumbers, liveIns);
  }

  void LiveValues::findLiveValuesAcrossEdge(
    const BasicBlock *pred,
    unsigned succno,
    ValueList &liveIns) const
  {
    const BasicBlock *bb = pred->getTerminator()->getSuccessor(succno);

    ValueSets::const_iterator i = OUT.find(bb);
    if( i == OUT.end() )
      return;

    BitVector liveins( i->second );
    liveins.resize( revNumbers.size() );
    addUsesFromPHI(pred, bb, liveins);

    translateBitVectorToValues(liveins, revNumbers, liveIns);
  }

  void LiveValues::addUsesFromPHI(const BasicBlock *pred, const BasicBlock *succ, BitVector &IN) const
  {
    // Also, OR-in the uses for the PHI nodes in this
    // successor.
    for(BasicBlock::const_iterator k=succ->begin(), z=succ->end(); k!=z; ++k)
    {
      const PHINode *phi = dyn_cast<PHINode>(&*k);
      if( !phi )
        break;

      const Value *use = phi->getIncomingValueForBlock(pred);
      assert(use);

      Value2Num::const_iterator l = numbers.find(use);
      if( l != numbers.end() )
        IN.set( l->second );
    }
  }

  void LiveValues::computeIN(const BasicBlock *bb, BitVector &IN) const
  {
    IN.resize( revNumbers.size() );
    for(GBI i=GB::child_begin(bb), e=GB::child_end(bb); i!=e; ++i)
    {
      const BasicBlock *succ = *i;
      ValueSets::const_iterator j = OUT.find(succ);
      if( j != OUT.end() )
        IN |= j->second;

      addUsesFromPHI(bb,succ, IN);
    }
  }

  void LiveValues::findLiveOutFromBB(
    const BasicBlock *bb,
    ValueList &liveIns) const
  {
    BitVector inbb;
    computeIN(bb,inbb);

    translateBitVectorToValues(inbb, revNumbers, liveIns);
  }

  void LiveValues::findLiveValuesAfterInst(
    const Instruction *inst,      // input
    ValueList &liveValues) const  // output
  {
    const BasicBlock *bb = inst->getParent();

    BitVector bitset;
    computeIN(bb, bitset);

    // visit instructions in REVERSE;
    BasicBlock::const_iterator j=bb->end(), f=bb->begin();
    while( j != f )
    {
      --j;
      const Instruction *def = &*j;

      if( def == inst )
        break;

      Value2Num::const_iterator i = numbers.find(def);
      if( i != numbers.end() )
      {
        // Record the defs
        bitset.reset( i->second );

        // Record the uses
        typedef Instruction::const_op_iterator OpIt;
        for(OpIt k=def->op_begin(), g=def->op_end(); k!=g; ++k)
        {
          const Value *use = *k;
          Value2Num::const_iterator i = numbers.find(use);
          if( i == numbers.end() )
            continue;

          bitset.set( i->second );
        }
      }
    }

    translateBitVectorToValues(
      bitset, revNumbers, liveValues);
  }

}
