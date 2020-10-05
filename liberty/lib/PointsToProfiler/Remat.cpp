#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"

#include "scaf/Utilities/GetOrInsertCIV.h"
#include "liberty/PointsToProfiler/Remat.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

bool Remat::canRematInHeader(const Value *ptr, const Loop *loop) const
{
  std::set<const Value*> already;
  /*
  Read spresults;
  HeapAssignment asgn;
  */
  return canRematInHeader(ptr,loop, /*spresults,asgn,*/ already);
}

/* Deprecated
bool Remat::canRematInHeader(const Value *ptr, const Loop *loop, const Read &spresults, const HeapAssignment &asgn) const
{
  std::set<const Value*> already;
  return canRematInHeader(ptr,loop,spresults,asgn,already);
}
*/

static void coerce(InstInsertPt &where, Value **A, Value **B)
{
  Value *factor1 = *A, *factor2 = *B;

  // Insert casts to correct types.
  if( IntegerType *ty1 = dyn_cast<IntegerType>(factor1->getType()) )
    if( IntegerType *ty2 = dyn_cast<IntegerType>(factor2->getType()) )
    {
      if( ty1->getBitWidth() > ty2->getBitWidth() )
      {
        // sign-extend factor2.
        Instruction *sext = new SExtInst(factor2, ty1);
        where << sext;
        *B = sext;
      }
      else if( ty2->getBitWidth() > ty1->getBitWidth() )
      {
        // sign-extend factor1
        Instruction *sext = new SExtInst(factor1, ty2);
        where << sext;
        *A = sext;
      }
    }
}

static Value *insertMul(InstInsertPt &where, Value *factor1, Value *factor2)
{
  coerce(where, &factor1, &factor2);
  BinaryOperator *product = BinaryOperator::CreateNSWMul(factor1,factor2);
  where << product;
  return product;
}

static Value *insertAdd(InstInsertPt &where, Value *addend1, Value *addend2)
{
  coerce(where, &addend1, &addend2);
  Instruction *add = BinaryOperator::CreateNSWAdd(addend1,addend2);
  where << add;
  return add;
}


bool Remat::canRematAtEntry(const Value *ptr, const Function *fcn, std::set<const Value*> &already) const
{
  if( already.count(ptr) )
    return true;
  already.insert(ptr);

  const BasicBlock *entry = &fcn->getEntryBlock();

  if( isa< Constant >(ptr) )
    return true;
  if( const Argument *arg = dyn_cast< Argument >(ptr) )
    if( arg->getParent() == fcn )
      return true;

  if( isa< PHINode >(ptr) )
    return false;

  if( const AllocaInst *alloca = dyn_cast< AllocaInst >(ptr) )
    return alloca->getParent() == entry;

  // All other instructions.
  if( const Instruction *inst = dyn_cast< Instruction >(ptr) )
  {
    if( inst->mayReadFromMemory() || inst->mayWriteToMemory() )
      return false;

    // The instruction is pure.  Can all operands be rematerialized?
    for(Instruction::const_op_iterator i=inst->op_begin(), e=inst->op_end(); i!=e; ++i)
      if( !canRematAtEntry(&**i,fcn, already) )
        return false;

    return true;
  }

  // All other cases.
  return false;
}

bool Remat::canRematInHeader(const Value *ptr, const Loop *loop, std::set<const Value*> &already) const
{
  if( already.count(ptr) )
    return true;
  already.insert(ptr);

  const BasicBlock *header = loop->getHeader();
  const Function *fcn = header->getParent();

  if( isa< Constant >(ptr) )
    return true;
  if( const Argument *arg = dyn_cast< Argument >(ptr) )
    if( arg->getParent() == fcn )
      return true;

  if( const PHINode *phi = dyn_cast< PHINode >(ptr) )
  {
    const BasicBlock *phibb = phi->getParent();

    // This is a PHI from /this/ loop
    if( phibb == header )
      return true;

    // It is live in (not from a subloop)
    else if( !loop->contains( phibb ) )
      return true;

    // cannot rematerialize
    else
      return false;
  }

  if( const AllocaInst *alloca = dyn_cast< AllocaInst >(ptr) )
    // Allocated OUTSIDE of the loop?
    return !loop->contains(alloca->getParent());

/*
  // Load instructions can be rematerialized iff they
  // load from read-only memory.
  if( asgn.isValidFor(loop) )
  {
    if( const LoadInst *load = dyn_cast< LoadInst >(ptr) )
    {
      HeapAssignment::Type heap = asgn.classify( load->getPointerOperand(), loop, spresults );
      if( heap == HeapAssignment::ReadOnly )
        return true;
    }
  }
*/

  // All other instructions.
  if( const Instruction *inst = dyn_cast< Instruction >(ptr) )
  {
    if( inst->mayReadFromMemory() || inst->mayWriteToMemory() )
      return false;

    // The instruction is pure.  Can all operands be rematerialized?
    for(Instruction::const_op_iterator i=inst->op_begin(), e=inst->op_end(); i!=e; ++i)
      if( !canRematInHeader(&**i,loop, /*spresults,asgn,*/ already) )
        return false;

    return true;
  }

  // All other cases.
  return false;
}

bool Remat::canRematAtBlock(const Value *ptr, const BasicBlock *bb, const DominatorTree *dt, std::set<const Value*> &already) const
{
  if( already.count(ptr) )
    return true;
  already.insert(ptr);

  const Function *fcn = bb->getParent();

  if( isa< Constant >(ptr) )
    return true;
  if( const Argument *arg = dyn_cast< Argument >(ptr) )
    if( arg->getParent() == fcn )
      return true;

  if( const Instruction *inst = dyn_cast< Instruction >(ptr) )
  {
    // Does the definition of this instruction dominate our new use?
    if( dt && dt->dominates(inst, bb) )
        return true;

    if( inst->mayReadFromMemory() || inst->mayWriteToMemory() )
      return false;

    // The instruction is pure.  Can all operands be rematerialized?
    for(Instruction::const_op_iterator i=inst->op_begin(), e=inst->op_end(); i!=e; ++i)
      if( !canRematAtBlock(&**i,bb,dt,already) )
        return false;

    return true;
  }

  // All other cases.
  return false;
}


Value *Remat::rematUnsafe(InstInsertPt &where, Value *ptr, const Loop *loop, const DominatorTree *dt)
{
  if( loop2clones[loop].count(ptr) )
    return loop2clones[loop][ptr];

  if( isa< Constant >(ptr) )
    return ptr;
  if( isa< Argument >(ptr) )
    return ptr;
  if( isa< PHINode >(ptr) )
    return ptr;
  if( isa< AllocaInst >(ptr) )
    return ptr;

  // All other instructions.
  if( Instruction *inst = dyn_cast< Instruction >(ptr) )
  {
    if( dt && dt->dominates(inst, where.getBlock() ) )
      return ptr;

    Instruction *clone = inst->clone();
    clone->setName("remat:" + inst->getName() + ":");
    loop2clones[loop][ptr] = clone; // insert BEFORE recur!!!

    // Rematerialize all operands.
    for(Instruction::const_op_iterator i=inst->op_begin(), e=inst->op_end(); i!=e; ++i)
    {
      Value *op = &**i;
      Value *new_op = rematUnsafe(where, op, loop);

      clone->replaceUsesOfWith(op, new_op);
    }

    where << clone;
    return clone;
  }

  assert(false && "How did we reach this point?!");
}


bool Remat::canEvaluateToInteger(const SCEV *s) const
{
  if( const SCEVCastExpr *cast = dyn_cast< SCEVCastExpr >(s) )
    return canEvaluateToInteger(cast->getOperand());
  else if( isa< SCEVConstant >(s) )
    return true;
  else if( const SCEVUnknown *unk = dyn_cast< SCEVUnknown >(s) )
  {
    Type *ty = 0;
    Constant *fieldno = 0;

    if ( unk->isSizeOf(ty) )
      return true;

    if( unk->isOffsetOf(ty,fieldno) )
      return true;

    if( unk->isAlignOf(ty) )
      return true;

    return false;
  }
  else if( const SCEVUDivExpr *udiv = dyn_cast< SCEVUDivExpr >(s) )
  {
    if( !canEvaluateToInteger(udiv->getLHS()) )
      return false;
    if( !canEvaluateToInteger(udiv->getRHS()) )
      return false;
    return ! udiv->getRHS()->isZero();
  }
  else if( const SCEVAddExpr *add = dyn_cast< SCEVAddExpr >(s) )
  {
    for(unsigned i=0; i<add->getNumOperands(); ++i)
      if( !canEvaluateToInteger( add->getOperand(i) ) )
        return false;
    return true;
  }
  else if( const SCEVMulExpr *mul = dyn_cast< SCEVMulExpr >(s) )
  {
    for(unsigned i=0; i<mul->getNumOperands(); ++i)
      if( !canEvaluateToInteger( mul->getOperand(i) ) )
        return false;
    return true;
  }
  else if( const SCEVSMaxExpr *max = dyn_cast< SCEVSMaxExpr >(s) )
  {
    for(unsigned i=0; i<max->getNumOperands(); ++i)
      if( !canEvaluateToInteger( max->getOperand(i) ) )
        return false;
    return true;
  }
  else if( const SCEVUMaxExpr *max = dyn_cast< SCEVUMaxExpr >(s) )
  {
    for(unsigned i=0; i<max->getNumOperands(); ++i)
      if( !canEvaluateToInteger( max->getOperand(i) ) )
        return false;
    return true;
  }


  return false;
}

uint64_t Remat::evaluateToIntegerUnsafe(const SCEV *s, const DataLayout &dl) const
{
  if( const SCEVCastExpr *cast = dyn_cast< SCEVCastExpr >(s) )
    return evaluateToIntegerUnsafe(cast->getOperand(), dl);
  else if( const SCEVConstant *cc = dyn_cast< SCEVConstant >(s) )
    return cc->getValue()->getLimitedValue();
  else if( const SCEVUnknown *unk = dyn_cast< SCEVUnknown >(s) )
  {
    Type *ty = 0;
    Constant *fieldno = 0;

    if ( unk->isSizeOf(ty) )
    {
      // Determine size of type ty
      return dl.getTypeStoreSize(ty);
    }

    if( unk->isOffsetOf(ty,fieldno) )
      if( ConstantInt *fno = dyn_cast< ConstantInt >( fieldno ) )
        // Determine offset of field.
        if( StructType *sty = dyn_cast< StructType >(ty) )
        {
          const StructLayout *layout = dl.getStructLayout(sty);
          return layout->getElementOffset( fno->getLimitedValue() );
        }

    if( unk->isAlignOf(ty) )
    {
      assert(false && "Not yet implemented: alignment");
    }
  }
  else if( const SCEVUDivExpr *udiv = dyn_cast< SCEVUDivExpr >(s) )
  {
    const uint64_t lhs = evaluateToIntegerUnsafe(udiv->getLHS(), dl);
    const uint64_t rhs = evaluateToIntegerUnsafe(udiv->getRHS(), dl);

    if( rhs == 0 )
      return ~0UL;
    else
      return lhs/rhs;
  }
  else if( const SCEVAddExpr *add = dyn_cast< SCEVAddExpr >(s) )
  {
    uint64_t sum=0;
    for(unsigned i=0; i<add->getNumOperands(); ++i)
    {
      const SCEV *op = add->getOperand(i);
      const uint64_t term = evaluateToIntegerUnsafe(op, dl);
      sum += term;
    }
    return sum;
  }
  else if( const SCEVMulExpr *mul = dyn_cast< SCEVMulExpr >(s) )
  {
    uint64_t prod=1;
    for(unsigned i=0; i<mul->getNumOperands(); ++i)
    {
      const SCEV *op = mul->getOperand(i);
      const uint64_t term = evaluateToIntegerUnsafe(op, dl);
      prod *= term;

      if( prod == 0 )
        break;
    }
    return prod;
  }
  else if( const SCEVSMaxExpr *max = dyn_cast< SCEVSMaxExpr >(s) )
  {
    int64_t mx = (int64_t) evaluateToIntegerUnsafe(max->getOperand(0), dl);

    for(unsigned i=1; i<max->getNumOperands(); ++i)
    {
      const SCEV *op = max->getOperand(i);
      const int64_t term = (int64_t) evaluateToIntegerUnsafe(op, dl);
      if( term > mx )
        mx = term;
    }
    return (uint64_t)mx;
  }
  else if( const SCEVUMaxExpr *max = dyn_cast< SCEVUMaxExpr >(s) )
  {
    uint64_t mx = evaluateToIntegerUnsafe(max->getOperand(0), dl);

    for(unsigned i=1; i<max->getNumOperands(); ++i)
    {
      const SCEV *op = max->getOperand(i);
      const uint64_t term = evaluateToIntegerUnsafe(op, dl);
      if( term > mx )
        mx = term;
    }
    return mx;
  }

  errs() << "Cant evaluate " << *s << '\n';
  assert(false);
}

Value *Remat::remat(InstInsertPt &where, ScalarEvolution &SE, const SCEV *s,
                    const DataLayout &dl) const {
  if( canEvaluateToInteger(s) )
    return ConstantInt::get(s->getType(), evaluateToIntegerUnsafe(s,dl) );

  else if( const SCEVConstant *cc = dyn_cast< SCEVConstant >(s) )
    return cc->getValue();

  else if( const SCEVUnknown *unk = dyn_cast< SCEVUnknown >(s) )
  {
    Value *v = unk->getValue();
    if( v->getType()->isPointerTy() )
    {
      LLVMContext &ctx = v->getContext();
      Type *u64 = Type::getInt64Ty(ctx);
      Instruction *cast = new PtrToIntInst(v,u64);
      where << cast;
      v = cast;
    }

    return v;
  }

  if( const SCEVSignExtendExpr *cast = dyn_cast< SCEVSignExtendExpr >(s) )
  {
    Value *op = remat(where, SE, cast->getOperand(), dl);
    Instruction *ii = new SExtInst(op, cast->getType());
    where << ii;
    return ii;
  }
  if( const SCEVTruncateExpr *cast = dyn_cast< SCEVTruncateExpr >(s) )
  {
    Value *op = remat(where, SE, cast->getOperand(), dl);
    Instruction *ii = new TruncInst(op, cast->getType());
    where << ii;
    return ii;
  }
  if( const SCEVZeroExtendExpr *cast = dyn_cast< SCEVZeroExtendExpr >(s) )
  {
    Value *op = remat(where, SE, cast->getOperand(), dl);
    Instruction *ii = new ZExtInst(op, cast->getType());
    where << ii;
    return ii;
  }

  else if( const SCEVUDivExpr *udiv = dyn_cast< SCEVUDivExpr >(s) )
  {
    Value *lhs = remat(where, SE, udiv->getLHS(), dl);
    Value *rhs = remat(where, SE, udiv->getRHS(), dl);

    Instruction *inst = BinaryOperator::CreateExactSDiv(lhs,rhs);
    where << inst;
    return inst;
  }
  else if( const SCEVAddExpr *add = dyn_cast< SCEVAddExpr >(s) )
  {
    Value *sum = 0;
    for(unsigned i=0; i<add->getNumOperands(); ++i)
    {
      const SCEV *op = add->getOperand(i);
      Value *term = remat(where, SE, op, dl);
      if( !sum )
        sum = term;
      else
        sum = insertAdd(where, sum,term);
    }

    return sum;
  }
  else if( const SCEVMulExpr *mul = dyn_cast< SCEVMulExpr >(s) )
  {
    Value *product = 0;
    for(unsigned i=0; i<mul->getNumOperands(); ++i)
    {
      const SCEV *op = mul->getOperand(i);
      Value *factor = remat(where, SE, op, dl);

      if( !product )
        product = factor;
      else
        product = insertMul(where, product,factor);
    }

    return product;
  }
  else if( const SCEVSMaxExpr *max = dyn_cast< SCEVSMaxExpr >(s) )
  {
    Value *product = 0;
    for(unsigned i=0; i<max->getNumOperands(); ++i)
    {
      const SCEV *op = max->getOperand(i);
      Value *factor = remat(where, SE, op, dl);

      if( !product )
        product = factor;
      else
      {
        // True if new max found.
        Instruction *cmp = 0;

        Type *opty = factor->getType();
        if( opty->isFloatingPointTy() )
          cmp = new FCmpInst(FCmpInst::FCMP_OGT, factor, product);

        else if( opty->isIntegerTy() )
          cmp = new ICmpInst(ICmpInst::ICMP_SGT, factor, product);

        else
          assert(false && "How to compare non-fp, non-int types for signed max?");

        where << cmp;

        SelectInst *mul = SelectInst::Create(cmp, factor, product );
        assert( mul->getTrueValue() == factor && "Too lazy to look it up");
        where << mul;
        product = mul;
      }
    }

    return product;
  }
  /*
  else if( const SCEVUMaxExpr *max = dyn_cast< SCEVUMaxExpr >(s) )
  {
    assert(false && "not yet implemented");
  }
  */
  else if( const SCEVAddRecExpr *addrec = dyn_cast< SCEVAddRecExpr >(s) )
  {
    Value *start = remat(where, SE, addrec->getStart(), dl);
    Value *step  = remat(where, SE, addrec->getStepRecurrence(SE), dl);
    Value *civ = getOrInsertCanonicalInductionVariable( addrec->getLoop() );

    Value *mul = insertMul(where, step,civ);
    Value *add = insertAdd(where, start,mul);

    return add;
  }

  errs() << "Cant rematerialize " << *s << '\n';
  assert(false);

}

Value *Remat::remat(InstInsertPt &where, ScalarEvolution &SE, const SCEV *s,
                    const DataLayout &dl, Type *ty) const {
  Value *rr = remat(where,SE,s,dl);

  if( !ty )
    return rr;
  if( rr->getType() == ty )
    return rr;

  if( ty->isPointerTy() )
  {
    Instruction *cast = new IntToPtrInst(rr,ty);
    where << cast;
    return cast;
  }

  else if( ty->isIntegerTy() )
  {
    Instruction *cast = new TruncInst(rr,ty);
    where << cast;
    return cast;
  }

  assert(false && "can't cast to that");
}

}
}


