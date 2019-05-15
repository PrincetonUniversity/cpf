#define DEBUG_TYPE "replace-constant-with-load"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"
#include "liberty/Utilities/SplitEdge.h"

#include <map>

namespace liberty
{
using namespace llvm;

static bool eliminateConstantUsers(Constant *gv, ReplaceConstantObserver &observer)
{
  // For each use
  for(Value::user_iterator j=gv->user_begin(), z=gv->user_end(); j!=z; ++j)
  {
    Value *v = &**j;
    ConstantExpr *cexpr = dyn_cast< ConstantExpr >(v);
    if( !cexpr )
      continue;

    // Make sure that constant has no constant users.
    // (it could be, for instance, (bitcast (gep (bitcast ...) ...) )
    //  or something horrendous like that)
    if( !eliminateConstantUsers(cexpr, observer) )
      return false;

    // At this point, we know we have a constant expression
    // which is NOT used by any constants.  I.e. it is only
    // used by instructions.

    // temporarily copy our uses, since we will modify the use list.
    typedef std::vector<Instruction*> UserList;
    UserList users;
    for(Value::user_iterator i=cexpr->user_begin(), e=cexpr->user_end(); i!=e; ++i)
    {
      Value *use = &**i;
      Instruction *inst = dyn_cast< Instruction >(use);
      assert( inst && "But... we already took care of this... <sob>");

      users.push_back( inst );
    }

    // We will now replace this constant with an equivalent instruction
    // By cases:

    if( cexpr->getOpcode() == Instruction::BitCast )
    {
      DEBUG(errs() << "Lowering constant bitcast to instruction: " << *cexpr << '\n');

      Value *operand = cexpr->getOperand(0);
      assert( operand == gv );
      Type *target = cexpr->getType();

      for(UserList::iterator i=users.begin(), e=users.end(); i!=e; ++i)
      {
        Instruction *inst = *i;
        DEBUG(errs() << "Changing " << *inst << '\n');

        if( PHINode *phi = dyn_cast< PHINode >(inst) )
        {
          // Unfortunately, we cannot insert before a PHI node because
          // all PHIs must be grouped at the top of a block.  In this
          // case, we will need to split an edge...
          for(unsigned pn=0, N=phi->getNumIncomingValues(); pn<N; ++pn)
          {
            if( phi->getIncomingValue(pn) == cexpr )
            {
              BasicBlock *pred = phi->getIncomingBlock(pn);
              BasicBlock *splitedge = split(pred,phi->getParent(), "replace.constant.with.load.");

              Instruction *cast = new BitCastInst(operand,target);

              InstInsertPt::Beginning(splitedge) << cast;
              phi->setIncomingValue(pn, cast);

              observer.addInstruction(cast, phi);
              observer.addInstruction(splitedge->getTerminator(),
                                      pred->getTerminator());
            }
          }
        }
        else
        {
          Instruction *cast = new BitCastInst(operand,target);
          InstInsertPt::Before(inst) << cast;
          inst->replaceUsesOfWith(cexpr, cast);
          observer.addInstruction(cast, inst);
        }

        DEBUG(errs() << "      to " << *inst << '\n');
      }

      // Kill this constant expr... sort-of
      assert( cexpr->use_empty() );
      cexpr->dropAllReferences();

      return false;
    }

    else if( GEPOperator *oper = dyn_cast< GEPOperator >(cexpr) )
    {
      DEBUG(errs() << "Lowering constant GEP to instruction: " << *cexpr << '\n');

      for(UserList::iterator i=users.begin(), e=users.end(); i!=e; ++i)
      {
        Instruction *inst = *i;
        DEBUG(errs() << "Changing " << *inst << '\n');

        Value *base = oper->getPointerOperand();
        std::vector<Value*> idx( oper->idx_begin(), oper->idx_end() );
        ArrayRef<Value *> args(idx);

        if( PHINode *phi = dyn_cast< PHINode >(inst) )
        {
          // Unfortunately, we cannot insert before a PHI node because
          // all PHIs must be grouped at the top of a block.  In this
          // case, we will need to split an edge...
          for(unsigned pn=0, N=phi->getNumIncomingValues(); pn<N; ++pn)
          {
            if( phi->getIncomingValue(pn) == cexpr )
            {
              BasicBlock *pred = phi->getIncomingBlock(pn);
              BasicBlock *splitedge = split(pred,phi->getParent(), "replace.constant.with.load.");

              Instruction *gep = 0;

              if( oper->isInBounds() )
                gep = GetElementPtrInst::CreateInBounds(base, args );
              else
                gep = GetElementPtrInst::Create(
                      cast<PointerType>(base->getType()->getScalarType())->getElementType(),
                                        base, args );


              InstInsertPt::Beginning(splitedge) << gep;
              phi->setIncomingValue(pn, gep);
              observer.addInstruction(gep, phi);
              observer.addInstruction(splitedge->getTerminator(),
                                      pred->getTerminator());
            }
          }
        }
        else
        {
          Instruction *gep = 0;
          if( oper->isInBounds() )
            gep = GetElementPtrInst::CreateInBounds(base, args );
          else
            gep = GetElementPtrInst::Create(
                  cast<PointerType>(base->getType()->getScalarType())->getElementType(),
                            base, args );


          InstInsertPt::Before(inst) << gep;
          inst->replaceUsesOfWith(cexpr, gep);
          observer.addInstruction(gep, inst);
        }

        DEBUG(errs() << "      to " << *inst << '\n');
      }

      // Kill this constant expr... sort-of
      assert( cexpr->use_empty() );
      cexpr->dropAllReferences();

      return false;
    }
  }

  return true;
}

bool replaceConstantWithLoad(Constant *gv, Value *gvptr, bool loadOncePerFcn)
{
  ReplaceConstantObserver null_observer;
  return replaceConstantWithLoad(gv,gvptr,null_observer,loadOncePerFcn);
}

bool replaceConstantWithLoad(Constant *gv, Value *gvptr, ReplaceConstantObserver &observer, bool loadOncePerFcn)
{
  // Since global variables are llvm::Constant objects,
  // and since normal code will sometimes cast them,
  // is possible that the global variables are
  // sometimes used by a ConstantExpr, such as a constant BitCast
  // or GEP.

  // This cannot hold when we replace it with a load instruction,
  // and so we must first lower those ConstantExprs into Instructions.
  for(;;)
    if( eliminateConstantUsers(gv,observer) )
      break;

  // Temporary copy of use list, since we modify it.
  typedef std::vector<Instruction*> UseList;
  UseList uses;
  for(Value::user_iterator j=gv->user_begin(), z=gv->user_end(); j!=z; ++j)
  {
    Value *v = &**j;

    Instruction *user = dyn_cast< Instruction >( v );
    if( !user )
      return false;

    uses.push_back( user );
  }

  // Foreach use
  std::map<Function*, LoadInst*> loads;
  for(UseList::iterator j=uses.begin(), z=uses.end(); j!=z; ++j)
  {
    Instruction *user = *j;

    // Instead of using the global variable,
    // load the gvptr, and use that.  Load
    // at most once per function, since it is
    // invariant across program execution.
    Function *userFcn = user->getParent()->getParent();
    LoadInst *load = 0;
    if( loads.count(userFcn) )
      load = loads[ userFcn ];
    else if( loadOncePerFcn )
    {
      load = new LoadInst(gvptr, gv->getName());

      InstInsertPt::Beginning(userFcn) << load;
      loads[userFcn] = load;
      observer.addInstruction(load, &userFcn->getEntryBlock().front() );
    }
    else
    {
      load = new LoadInst(gvptr, gv->getName());

      InstInsertPt::Before(user) << load;
      observer.addInstruction(load,user);
    }

    user->replaceUsesOfWith(gv, load);
  }

  return true;
}

}

