#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/SplitEdge.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Transforms/Utils/Cloning.h"

namespace liberty
{
  using namespace llvm;

  static BasicBlock *fixLandingPad(BasicBlock *from, BasicBlock *to, StringRef prefix, InvokeInst *invoke, LoopInfo *updateLoopInfo = 0, Loop *destination_loop = 0)
  {
//    errs() << "Splitting:\n" << *from << "\n" << *to << "\n";

    LandingPadInst *lpad = cast< LandingPadInst >( to->getFirstNonPHI() );
    //sot
    //BasicBlock *after = to->splitBasicBlock( * lpad->getNextNode(), "split.lpad." );
    BasicBlock *after = to->splitBasicBlock( lpad->getNextNode(), "split.lpad." );
    after->moveAfter(to);

    if( updateLoopInfo && destination_loop )
    {
      LoopInfoBase<BasicBlock, Loop> &updateLoopInfoBase = *updateLoopInfo;
      if( ! destination_loop->contains(after) ) // How can it contain the new loop?
        destination_loop->addBasicBlockToLoop(after, updateLoopInfoBase);
        //destination_loop->addBasicBlockToLoop(after, updateLoopInfo->getBase());
    }

    // Add PHI-nodes in after
    typedef std::map<Instruction*,PHINode*> I2Phi;
    I2Phi phis;
    InstInsertPt where = InstInsertPt::Beginning(after);
    for(BasicBlock::iterator i=to->begin(), e=to->end(); i!=e; ++i)
    {
      Instruction *inst = &*i;
      if( inst->use_empty() )
        continue;

      PHINode *phi = PHINode::Create(inst->getType(),1);
      phis[inst] = phi;
      where << phi;
    }

    // If 'to' has more than one predecessor, we need to duplicate it for
    // each predecessor.
    typedef std::map<BasicBlock*,BasicBlock*> BB2BB;
    BB2BB new_tos;
    for(Value::user_iterator i=to->user_begin(), e=to->user_end(); i!=e; ++i)
    {
      Instruction *pred_term = dyn_cast< Instruction >( &**i );
      if( !pred_term->isTerminator() )
        continue;
      BasicBlock *pred = pred_term->getParent();

      if( new_tos.count(pred) )
        continue;

      else
      {
        ValueToValueMapTy vmap;
        BasicBlock *new_to = CloneBasicBlock(to, vmap, ".critical-lpad.", to->getParent());
        new_tos[pred] = new_to;

        if( updateLoopInfo && destination_loop )
        {
          //sot
          LoopInfoBase<BasicBlock, Loop> &updateLoopInfoBase = *updateLoopInfo;
          //destination_loop->addBasicBlockToLoop(new_to, updateLoopInfo->getBase());
          destination_loop->addBasicBlockToLoop(new_to, updateLoopInfoBase);
        }

        new_to->moveBefore(after);

        for(I2Phi::iterator j=phis.begin(), z=phis.end(); j!=z; ++j)
        {
          Instruction *orig = j->first;
          Instruction *clone = cast<Instruction>( &*vmap[orig] );

          PHINode *phi = j->second;
          phi->addIncoming(clone, new_to);
        }
      }
    }

    // Remove excess predecessors
    for(BB2BB::iterator i=new_tos.begin(), e=new_tos.end(); i!=e; ++i)
    {
      BasicBlock *my_pred = i->first;
      BasicBlock *new_to = i->second;

      my_pred->getTerminator()->replaceUsesOfWith(to, new_to);

      for(BasicBlock::iterator j=new_to->begin(), z=new_to->end(); j!=z; ++j)
      {
        PHINode *phi = dyn_cast< PHINode >( &*j );
        if( !phi )
          continue;

        for(BB2BB::iterator k=new_tos.begin(); k!=e; ++k)
        {
          BasicBlock *pred = k->first;
          if( pred != my_pred )
            if( phi->getBasicBlockIndex(pred) != -1 )
              phi->removeIncomingValue(pred);
        }
      }
    }

    /*
    errs() << "Into:\n";
    errs() << *from << "\n";
    for(BB2BB::iterator i=new_tos.begin(), e=new_tos.end(); i!=e; ++i)
      errs() << * i->second << "\n";
    errs() << *after << "\n";
    */

    for(I2Phi::iterator i= phis.begin(), e=phis.end(); i!=e; ++i)
      i->first->replaceAllUsesWith(i->second);
    to->dropAllReferences();
    to->eraseFromParent();

    BasicBlock *new_from = new_tos[from];
    assert( new_from->getTerminator()->getNumSuccessors() == 1 );
    return split( new_from, 0u, prefix, updateLoopInfo);
  }


  BasicBlock *split(BasicBlock *from, BasicBlock *to, DominatorTree &dt, DominanceFrontier &df, StringRef prefix)
  {
    BasicBlock *sync = split(from,to,prefix);

    dt.addNewBlock(sync, from);
    if( dt[to]->getIDom() == dt[from] )
    {
      dt.changeImmediateDominator(to, sync);

      //  new dom old
      DominanceFrontier::iterator i = df.find(to);
      if( i != df.end() )
      {
        df.addBasicBlock(sync, i->second);
        if( i->second.count(to) )
          df.removeFromFrontier( df.find(sync), to );
      }
      else
      {
        df.addBasicBlock(sync, DominanceFrontier::DomSetType() );
      }
    }
    else
    {
      // new !dom old
      DominanceFrontier::DomSetType singleton;
      singleton.insert(to);
      df.addBasicBlock(sync,singleton);
    }

    return sync;
  }

  BasicBlock *split(BasicBlock *from, BasicBlock *to, DominatorTree &dt, StringRef prefix)
  {
    BasicBlock *sync = split(from,to,prefix);

    dt.addNewBlock(sync, from);
    if( dt[to]->getIDom() == dt[from] )
    {
      dt.changeImmediateDominator(to, sync);
      dt.changeImmediateDominator(sync, from);
    }

    return sync;
  }


  BasicBlock *split(BasicBlock *from, BasicBlock *to, StringRef prefix)
  {
    //sot
    //if( !prefix )
    if( prefix.empty() )
      prefix = "split.";

    // If this is an exceptional return from an invoke instruction,
    // then the destination's first non-phi must be a landing pad instruction.
    // Splitting the edge will break that invariant.
    Instruction *term = from->getTerminator();
    if( InvokeInst *invoke = dyn_cast< InvokeInst >(term) )
      if( invoke->getUnwindDest() == to )
        return fixLandingPad(from,to,prefix,invoke);

    LLVMContext &Context = from->getContext();
    BasicBlock *sync = BasicBlock::Create(Context,
      Twine(prefix) + from->getName(), from->getParent());

    // Put this split after the source block.
    sync->moveAfter(from);

    term->replaceUsesOfWith(to,sync);
    BranchInst::Create(to, sync);

    for(BasicBlock::iterator i=to->begin(), e=to->end(); i!=e; ++i)
      if( PHINode *phi = dyn_cast<PHINode>( &*i ) )
      {
        unsigned numEdges = 0;
        for(int pn=phi->getNumIncomingValues()-1; pn>=0; --pn)
          if( from == phi->getIncomingBlock(pn) )
          {
            if( numEdges == 0 )
              phi->setIncomingBlock(pn, sync );
            else
              phi->removeIncomingValue(pn, false);
            ++numEdges;
          }
      }

    return sync;
  }

  BasicBlock *split(BasicBlock *from, unsigned succno, StringRef prefix, LoopInfo *updateLoopInfo)
  {
    //sot
    //if( !prefix )
    if( prefix.empty() )
      prefix = "split.";

    Instruction *term = from->getTerminator();
    BasicBlock *to = term->getSuccessor(succno);

    Loop *destination_loop = 0;
    if( updateLoopInfo )
    {
      Loop *a = updateLoopInfo->getLoopFor( from );
      Loop *b = updateLoopInfo->getLoopFor( to   );

      if( a && b )
      {
        if( a->getLoopDepth() <= b->getLoopDepth() )
          destination_loop = a;
        else
          destination_loop = b;
      }
    }

    // If this is an exceptional return from an invoke instruction,
    // then the destination's first non-phi must be a landing pad instruction.
    // Splitting the edge will break that invariant.
    if( InvokeInst *invoke = dyn_cast< InvokeInst >(term) )
      if( invoke->getUnwindDest() == to )
        return fixLandingPad(from,to,prefix,invoke,updateLoopInfo,destination_loop);

    LLVMContext &Context = from->getContext();
    BasicBlock *sync = BasicBlock::Create(Context,
      Twine(prefix) + from->getName(), from->getParent());

    // Put this split after the source block.
    sync->moveAfter(from);

    term->setSuccessor(succno,sync);
    BranchInst::Create(to, sync);

    for(BasicBlock::iterator i=to->begin(), e=to->end(); i!=e; ++i)
    {
      PHINode *phi = dyn_cast<PHINode>( &*i );
      if( !phi )
        break;

      for(unsigned pn=0, PN=phi->getNumIncomingValues(); pn<PN; ++pn)
        if( from == phi->getIncomingBlock(pn) )
        {
          phi->setIncomingBlock(pn, sync );
          break;
        }
    }

    if( updateLoopInfo && destination_loop )
    {
      LoopInfoBase<BasicBlock, Loop> &updateLoopInfoBase = *updateLoopInfo;
      // How can the loop already contain the newly created basic block?
      if( ! destination_loop->contains(sync) )
        destination_loop->addBasicBlockToLoop( sync, updateLoopInfoBase );
        //destination_loop->addBasicBlockToLoop( sync, updateLoopInfo->getBase() );
    }

    return sync;
  }
}
