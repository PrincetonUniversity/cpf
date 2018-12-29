#include <list>

#include "llvm/IR/Instructions.h"

#include "liberty/Utilities/AllocaHacks.h"

namespace liberty {

  using namespace llvm;


  typedef std::vector<User*> Users;
  typedef Users::iterator UsersIt;
  typedef Value::user_iterator UI;

  typedef std::pair<BasicBlock*,Value*> BB2Val;
  typedef std::vector<BB2Val> VerySmallBBMap;
  typedef VerySmallBBMap::iterator VSBBMI;

  void replaceUsesWithinFcnWithLoadFromAlloca(Value *oldValue, Function *scope, Value *alloca) {
    // and replace ALL uses of this value within this fcn
    // with the a load from the stack slot

    // copy uses, since the use iterator will fail upon modification
    Users users(oldValue->user_begin(), oldValue->user_end());

    // Now, handle all PHI nodes first.
    // PHI nodes are different from other uses in two ways:
    //  (1) PHIs must be grouped at the beginning of a basic block.
    //      As a result, the load from the alloca must occur in the
    //      predecessor block.
    //  (2) PHIs may have multiple entries from the same predecessor
    //      block, provided they are identical values.  This can occur,
    //      for instance, when the predecessor ends in a switch and
    //      several of the branch targets are the same successor.
    //      In this case, we must ensure that we load only once
    //      in the predecessor block, or we will introduce two
    //      /different/ values on two different control flow edges
    //      from the same basic block.
    for(UsersIt i=users.begin(), e=users.end(); i!=e; ++i)
    {
      PHINode *phi = dyn_cast< PHINode >( *i );
      if( !phi )
        continue;

      BasicBlock *bb = phi->getParent();

      // Within scope?
      if( bb->getParent() != scope )
        continue;

      // Now consider all incoming (val,bb) pairs
      VerySmallBBMap uniquePreds;
      for(unsigned j=0; j<phi->getNumIncomingValues(); ++j)
      {
        // Is it a use of oldValue?
        Value *inVal = phi->getIncomingValue(j);
        if( inVal != oldValue )
          continue;

        BasicBlock *inBB = phi->getIncomingBlock(j);

        // If we already loaded this, then re-use that load.
        bool fixed = false;
        for(VSBBMI k=uniquePreds.begin(), f=uniquePreds.end(); k!=f; ++k)
        {
          // We have already seen (x,inBB) in the phi's incoming list.
          if( k->first == inBB )
          {
            // We have ALREADY loaded it.
            phi->setIncomingValue(j, k->second);
            fixed = true;
            break;
          }
        }

        if( fixed )
          continue;

        // We have NOT already loaded it.
        // Insert the load at the end of the predecessor block.
        Value *load = new LoadInst(alloca, "alloca.hacks.phi.",
                           inBB->getTerminator() );
        phi->setIncomingValue(j, load);

        // save it for later.
        uniquePreds.push_back( BB2Val(inBB,load) );
      }
    }

    // Now, we handle every use which is not a PHI node.
    // For each use...
    for(UsersIt k=users.begin(), e=users.end(); k!=e; ++k) {
      User *user = *k;

      if( isa<PHINode>(user) )
        continue;

      // ...by an instruction...
      if( Instruction *instr = dyn_cast<Instruction>( user ) ) {

        // Exception:
        // If this use already is a store to the alloca slot.
        // This supports multiple calls to replaceUsesWithinFcnWithLoadFromAlloca()
        if( StoreInst *store = dyn_cast<StoreInst>(instr) )
          if( store->getPointerOperand() == alloca )
            continue;

        // ...within thread 0...
        if( instr->getParent()->getParent() == scope ) {
          // insert a load just before the use
          Value *load = new LoadInst( alloca, "alloca.hacks.", instr );
          user->replaceUsesOfWith( oldValue, load );
        }
      }
    }
  }

  // Replace every use of a given oldValue with a newValue
  // within the confines of function scope.
  void replaceUsesWithinFcn( Value *oldValue, Function *scope, Value *newValue ) {
    // Collect a set of live-in uses which must be replaced
    Users userSet;
    for(UI j=oldValue->user_begin(); j!=oldValue->user_end(); ++j) {
      User *user = *j;
      // If this use is an instruction...
      if( Instruction *instr = dyn_cast<Instruction>(user) ) {
        // ... within this thread, then
        if( instr->getParent()->getParent() == scope ) {
          // privatize it!
          userSet.push_back(user);
        }
      }
    }

    // replace each use
    for (UsersIt j=userSet.begin(), e=userSet.end(); j!=e; ++j)
      (*j)->replaceUsesOfWith( oldValue, newValue );
  }

}
