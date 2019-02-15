#define DEBUG_TYPE  "specpriv-reduction"

#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Redux/Reduction.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/SplitEdge.h"
#include "PDG.hpp"


#include <list>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

static cl::opt<bool> AllowFloatingPointReduction(
  "float-reduction",
  cl::init(true),
  cl::NotHidden,
  cl::desc("Assume floating-point arithmetic is associative and commutative"));

static cl::opt<std::string> TargetFcn(
  "spec-priv-reductions-fcn", cl::init(""), cl::NotHidden,
  cl::desc("Refine this function"));

static cl::opt<std::string> TargetLoop(
  "spec-priv-reductions-loop", cl::init(""), cl::NotHidden,
  cl::desc("Refine this loop"));

static cl::opt<bool> VerifyEachFcn(
  "spec-priv-reductions-verify", cl::init(false), cl::NotHidden,
  cl::desc("Verify after each function"));

STATISTIC(numRedux, "Register redux operations lowered to memory redux operations");
STATISTIC(numCanon, "Canonicalized memory reductions");
STATISTIC(numRemat, "Binop rematerialized because use outside of loop");

// STATISTIC(numPrivate, "Private registers lowered to private memory operations");

Reduction::Type Reduction::isAssocAndCommut(const BinaryOperator *add)
{
  if( AllowFloatingPointReduction && add->getOpcode() == Instruction::FAdd )
  {
    if( add->getType()->isFloatTy() )
      return Add_f32;
    else if( add->getType()->isDoubleTy() )
      return Add_f64;
  }

  else if( add->getOpcode() == Instruction::Add )
  {
    if( add->getType()->isIntegerTy(8) )
      return Add_i8;
    else if( add->getType()->isIntegerTy(16) )
      return Add_i16;
    else if( add->getType()->isIntegerTy(32) )
      return Add_i32;
    else if( add->getType()->isIntegerTy(64) )
      return Add_i64;
  }

  return NotReduction;
}

Reduction::Type Reduction::isAssocAndCommut(const CmpInst *cmp)
{
  // TODO I should make a distinction
  // between ordered and unordered comparisons, which
  // treat the NaN corner case differently.

  const llvm::Type *opty = cmp->getOperand(0)->getType();

  CmpInst::Predicate p = cmp->getPredicate();
  switch(p)
  {
    case CmpInst::FCMP_OGT:
    case CmpInst::FCMP_UGT:
    case CmpInst::FCMP_OGE:
    case CmpInst::FCMP_UGE:
      if( opty->isFloatTy() )
        return Max_f32;
      else if( opty->isDoubleTy() )
        return Max_f64;
      break;

    case CmpInst::FCMP_OLT:
    case CmpInst::FCMP_ULT:
    case CmpInst::FCMP_OLE:
    case CmpInst::FCMP_ULE:
      if( opty->isFloatTy() )
        return Min_f32;
      else if( opty->isDoubleTy() )
        return Min_f64;
      break;

    case CmpInst::ICMP_UGT:
    case CmpInst::ICMP_UGE:
      if( opty->isIntegerTy(8) )
        return Max_u8;
      else if( opty->isIntegerTy(16) )
        return Max_u16;
      else if( opty->isIntegerTy(32) )
        return Max_u32;
      else if( opty->isIntegerTy(64) )
        return Max_u64;
      break;

    case CmpInst::ICMP_SGT:
    case CmpInst::ICMP_SGE:
      if( opty->isIntegerTy(8) )
        return Max_i8;
      else if( opty->isIntegerTy(16) )
        return Max_i16;
      else if( opty->isIntegerTy(32) )
        return Max_i32;
      else if( opty->isIntegerTy(64) )
        return Max_i64;
      break;

    case CmpInst::ICMP_ULT:
    case CmpInst::ICMP_ULE:
      if( opty->isIntegerTy(8) )
        return Min_u8;
      else if( opty->isIntegerTy(16) )
        return Min_u16;
      else if( opty->isIntegerTy(32) )
        return Min_u32;
      else if( opty->isIntegerTy(64) )
        return Min_u64;
      break;

    case CmpInst::ICMP_SLT:
    case CmpInst::ICMP_SLE:
      if( opty->isIntegerTy(8) )
        return Min_i8;
      else if( opty->isIntegerTy(16) )
        return Min_i16;
      else if( opty->isIntegerTy(32) )
        return Min_i32;
      else if( opty->isIntegerTy(64) )
        return Min_i64;
      break;

    default:
      break;
  }

  return NotReduction;
}

// swap min/max
Reduction::Type Reduction::reverse(Reduction::Type rt)
{
  if( Max_i8 <= rt && rt <= Max_f64 )
    return (Type) (rt - Max_i8 + Min_i8);

  else if( Min_i8 <= rt && rt <= Min_f64 )
    return (Type) (rt - Min_i8 + Max_i8);

  else
    return rt;
}

Reduction::Type Reduction::isReduction(const LoadInst *load, const CmpInst *cmp, const BranchInst *br, const StoreInst *store)
{
  if( !load || !cmp || !br || !store)
    return NotReduction;
  if( !load->hasOneUse() )
    return NotReduction;
  if( load->getPointerOperand() != store->getPointerOperand() )
    return NotReduction;
  if( !cmp->hasOneUse() )
    return NotReduction;
  if( br->isUnconditional() )
    return NotReduction;
  if( br->getCondition() != cmp )
    return NotReduction;

  Value *other = 0;
  if( cmp->getOperand(0) == load )
    other = cmp->getOperand(1);
  else if( cmp->getOperand(1) == load )
    other = cmp->getOperand(0);
  else
    return NotReduction;

  if( store->getValueOperand() != other )
    return NotReduction;

  Type rt = isAssocAndCommut(cmp);
  if( rt == NotReduction )
    return NotReduction;

  if( load == cmp->getOperand(1) )
    rt = reverse(rt);

  const BasicBlock *storebb = store->getParent();
  if( !storebb->getSinglePredecessor() )
    return NotReduction;

  if( br->getSuccessor(0) == storebb )
    rt = reverse(rt);
  else if( br->getSuccessor(1) != storebb )
  { /* good */ }
  else
    return NotReduction;

  return rt;
}

Reduction::Type Reduction::isReduction(const LoadInst *load, const BinaryOperator *add, const StoreInst *store)
{
  if( ! load )
    return NotReduction;

  if( ! load->hasOneUse() )
    return NotReduction;

  if( load->getPointerOperand() != store->getPointerOperand() )
    return NotReduction;

  if( !add->hasOneUse() )
    return NotReduction;

  if( add != store->getValueOperand() )
    return NotReduction;

  return isAssocAndCommut(add);
}

Reduction::Type Reduction::isReductionLoad(
  const LoadInst *load,
  /* optional output parameters */
  const BinaryOperator **add_out,
  const CmpInst **cmp_out,
  const BranchInst **br_out,
  const StoreInst **st_out)
{
  if( load->use_empty() )
    return NotReduction;

  const Value *luser = * load->user_begin();
  if( const BinaryOperator *add = dyn_cast< BinaryOperator >( luser ) )
  {
    const StoreInst *store = dyn_cast< StoreInst >( * add->user_begin() );
    if( !store )
      return NotReduction;

    if( Type t = isReduction(load, add, store) )
    {
      // One last pesky case:
      //    t0 = load x
      //    t1 = load x
      //    t2 = t0 + t1
      //    store t2, x
      // Is not a reduction.  In this case, we want to
      // consistently say that if the 0-th arg of the add
      // forms a reduction, then the 1-th arg may not!
      if( load == add->getOperand(1) )
        if( LoadInst *load0 = dyn_cast< LoadInst >( add->getOperand(0) ) )
          if( isReduction(load0, add, store) )
            return NotReduction;

      // Save output parameters.
      if( add_out )
        *add_out = add;
      if( st_out )
        *st_out = store;

      return t;
    }
  }

  else if( const CmpInst *cmp = dyn_cast< CmpInst >( luser ) )
  {
    const BranchInst *br = dyn_cast< BranchInst >( * cmp->user_begin() );
    if( !br )
      return NotReduction;

    const Value *other = cmp->getOperand( load == cmp->getOperand(0) );

    // FIXME This does not handle the case in which the store uses not the given load
    // but a newly loaded value from the same location.
    // For example, this code assumes this kind of structure:
    // %load = load from some location
    // %cmp = icmp sgt i32 %somevalue, %load, ...
    // ...
    // %store = store %load to some location
    // But if %store does not use the already loaded value %load directly and loads
    // the value again, this code wil lnot detect it.
    // Maybe CSE can solve it?

    // Find the store.  Search the uses of /other/.
    for(Value::const_use_iterator i=other->use_begin(), e=other->use_end(); i!=e; ++i)
    {
      const StoreInst *store = dyn_cast< StoreInst >( i->getUser() );
      if( !store )
        continue;
      const BasicBlock *storebb = store->getParent();
      if( storebb != br->getSuccessor(0)
      &&  storebb != br->getSuccessor(1) )
        continue;

      // looks like we found one?
      if( Type t = isReduction(load,cmp,br,store) )
      {
        // yes, this is a reduction.
        // make sure that /other/ is not also a reduction!
        // FIXME heejin: what if load == cmp->getOperand(0)?
        if( load == cmp->getOperand(1) )
          if( LoadInst *load0 = dyn_cast< LoadInst >( cmp->getOperand(0) ) )
            if( isReduction(load0,cmp,br,store) )
              return NotReduction;

      if( cmp_out )
        *cmp_out = cmp;
      if( br_out )
        *br_out = br;
      if( st_out )
        *st_out = store;

        return t;
      }
    }
  }

  return NotReduction;
}

Reduction::Type Reduction::isReductionStore(const StoreInst *store)
{
  // sum reduction?
  if( const BinaryOperator *add = dyn_cast< BinaryOperator >( store->getValueOperand() ) )
  {
    // Check each operand of add.
    if( const LoadInst *load = dyn_cast< LoadInst >( add->getOperand(0) ) )
      if( Type t = isReduction(load, add, store) )
        return t;

    if( const LoadInst *load = dyn_cast< LoadInst >( add->getOperand(1) ) )
      if( Type t = isReduction(load, add, store) )
        return t;
  }

  // min/max reduction?
  const BasicBlock *storebb = store->getParent();
  if( const BasicBlock *pred = storebb->getSinglePredecessor() )
    if( const BranchInst *br = dyn_cast< BranchInst >( pred->getTerminator() ) )
      if( br->isConditional())
        if( const CmpInst *cmp = dyn_cast< CmpInst >( br->getCondition() ) )
        {
          if( const LoadInst *load = dyn_cast< LoadInst >( cmp->getOperand(0) ) )
            if( Type t = isReduction(load,cmp,br,store) )
              return t;

          if( const LoadInst *load = dyn_cast< LoadInst >( cmp->getOperand(1) ) )
            if( Type t = isReduction(load,cmp,br,store) )
              return t;
        }

  return NotReduction;
}


// Ugly.  Too bad.
Value *Reduction::getIdentityValue(Type ty, LLVMContext &ctx)
{
  switch(ty)
  {
    // addition, unsigned max: identity is zero
    case Add_i8:
    case Max_u8:
      return ConstantInt::get( llvm::Type::getInt8Ty(ctx), 0 );

    case Add_i16:
    case Max_u16:
      return ConstantInt::get( llvm::Type::getInt16Ty(ctx), 0 );

    case Add_i32:
    case Max_u32:
      return ConstantInt::get( llvm::Type::getInt32Ty(ctx), 0 );

    case Add_i64:
    case Max_u64:
      return ConstantInt::get( llvm::Type::getInt64Ty(ctx), 0 );

    // fp add: identity is zero
    case Add_f32:
      return ConstantFP::get( llvm::Type::getFloatTy(ctx), 0.0 );

    case Add_f64:
      return ConstantFP::get( llvm::Type::getDoubleTy(ctx), 0.0 );

    // unsigned min: identity is the biggest integer (all ones value)
    case Min_u8:
      return ConstantInt::get( llvm::Type::getInt8Ty(ctx), 0x0ffu );

    case Min_u16:
      return ConstantInt::get( llvm::Type::getInt16Ty(ctx), 0x0ffffu );

    case Min_u32:
      return ConstantInt::get( llvm::Type::getInt32Ty(ctx), 0x0ffffffffu );

    case Min_u64:
      return ConstantInt::get( llvm::Type::getInt64Ty(ctx), 0x0ffffffffffffffffu );

    // signed-min: identity is the most postitive integer
    case Min_i8:
      return ConstantInt::get( llvm::Type::getInt8Ty(ctx), 0x07fu );

    case Min_i16:
      return ConstantInt::get( llvm::Type::getInt16Ty(ctx), 0x07fffu );

    case Min_i32:
      return ConstantInt::get( llvm::Type::getInt32Ty(ctx), 0x07fffffffu );

    case Min_i64:
      return ConstantInt::get( llvm::Type::getInt64Ty(ctx), 0x07fffffffffffffffu );

    // fp min: identity is positive infinity
    case Min_f32:
      return ConstantFP::getInfinity( llvm::Type::getFloatTy(ctx) );

    case Min_f64:
      return ConstantFP::getInfinity( llvm::Type::getDoubleTy(ctx) );

    // signed-max: identity is the most negative integer
    case Max_i8:
      return ConstantInt::get( llvm::Type::getInt8Ty(ctx), 0x080u );

    case Max_i16:
      return ConstantInt::get( llvm::Type::getInt16Ty(ctx), 0x08000u );

    case Max_i32:
      return ConstantInt::get( llvm::Type::getInt32Ty(ctx), 0x080000000u );

    case Max_i64:
      return ConstantInt::get( llvm::Type::getInt64Ty(ctx), 0x08000000000000000u );

    // fp max: identity is negative infinity
    case Max_f32:
      return ConstantFP::getInfinity( llvm::Type::getFloatTy(ctx), true );

    case Max_f64:
      return ConstantFP::getInfinity( llvm::Type::getDoubleTy(ctx), true );

    case NotReduction:
      assert(false);
  }

  return 0;
}

// This function is used to determine if there is a dependence from the given
// instruction to the loop backedge. To be consistent with the old spec,
// PDG can be NULL, and in this case it assumes there is a dependence.
// To this code to work, the loop should have a loop-rotated form with
// -loop-rotate, i.e., the loop latch should end with a conditional branch.
// If it does not have the rotated form, this just returns true to be
// consistent with the old spec.
static bool affectsLoopBackEdge(const Instruction *inst, const Loop *loop, const llvm::PDG *pdg)
{
  if (!pdg)
    return true;
  BasicBlock *latch = loop->getLoopLatch();
  BranchInst *backEdge = dyn_cast<BranchInst>(latch->getTerminator());
  if (!backEdge)
    return true;
  // TODO: there should be a const version of fetchNode
  llvm::PDG *non_const_pdg = const_cast<llvm::PDG*>(pdg);
  auto instNode = non_const_pdg->fetchNode((Value *) inst);
  for (auto edge : instNode->getOutgoingEdges()) {
    Instruction *incomingInst = dyn_cast<Instruction>(edge->getIncomingT());
    if (incomingInst == (Instruction*) backEdge) {
      if (!edge->isMemoryDependence() && !edge->isControlDependence() && !edge->isLoopCarriedDependence())
        return true;
    }
  }
  return false;
}

bool Reduction::isRegisterReduction(
    /* Inputs */  ScalarEvolution &scev, Loop *loop, PHINode *phi0, const llvm::PDG *pdg, const std::set<PHINode*> &ignore,
    /* Outputs */ Reduction::Type &rt, BinaryOperator::BinaryOps &reductionOpcode,  VSet &u_phis, VSet &u_binops, VSet &u_cmps, VSet &u_brs, VSet &used_outside, Value* &initial_value)
{
  if( ignore.count(phi0) )
    return false;

  // How the given PDG is used: (it can be NULL)
  // If there is no PDG given (pdg == NULL), we just assume it as an induction
  // variable if it has the right form. (var += something)
  // If there is a PDG given, check if it really is one - check if it affects
  // the backedge in addition to the form check.

  if( phi0 == loop->getCanonicalInductionVariable() &&
      affectsLoopBackEdge(phi0, loop, pdg) )
  {
    DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 1; it's the canonical induction variable\n");
    return false;
  }

  if( ! phi0->getType()->isIntegerTy()
  &&  ! phi0->getType()->isFloatTy()
  &&  ! phi0->getType()->isDoubleTy() )
  {
    DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 2; not a reducible type\n");
    // sot : causes false negatives in ks. 8,13,14,18 and final assertion causes false negatives as well
    // For ks, variables maxA, maxPrevA, maxB and maxPrevB that are pointers
    // won't be detected as reduction objects. Not exactly a classic reduction
    // scenario (other reduction object is used in the comparison but
    // updated similarly and the loop-carried deps can still be removed).
    // If changed need to ensure that it does not lead to false positives
    return false;
  }

  // Is it an induction variable (may not have been canonicalized)
  if( scev.isSCEVable( phi0->getType() ) )
  {
    const SCEV *ss = scev.getSCEVAtScope(phi0, loop);
    if( ss )
      if( const SCEVAddRecExpr *induc = dyn_cast< SCEVAddRecExpr >(ss) )
        if( induc->getLoop() == loop )
          if( isa< SCEVConstant >( induc->getStepRecurrence(scev) ) )
            if( affectsLoopBackEdge(phi0, loop, pdg) )
            {
              DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 1b: it's a (non canonical) induction variable\n");
              return false;
            }
  }

  // Find the initial (live-in) value.
  for(unsigned i=0; i<phi0->getNumIncomingValues(); ++i)
  {
    BasicBlock *bb = phi0->getIncomingBlock(i);
    Value *v = phi0->getIncomingValue(i);

    if( loop->contains(bb) )
      continue;

    if( initial_value != v && initial_value != 0)
    {
     DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 3 because inconsistent initial values:\n"
                   << "  o def1: " << *initial_value << '\n'
                   << "  o def2: " << *v << '\n');
      return false;
    }

    initial_value = v;
  }
  if( !initial_value )
  {
    DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 4 because no initial value\n");
    return false;
  }

  // Forward search: find all uses, transitive, within this loop.
  Fringe fringe;
  rt = Reduction::NotReduction;
  fringe.push_back(phi0);
  while( !fringe.empty() )
  {
    Instruction *inst = fringe.back();
    fringe.pop_back();

    if( !loop->contains(inst->getParent()) )
      continue; // not within loop
    if( u_phis.count(inst) || u_binops.count(inst) || u_cmps.count(inst) ||
        u_brs.count(inst) )
      continue; // already visited

    if( PHINode *phi = dyn_cast< PHINode >(inst) )
    {
      u_phis.insert(phi);
      //DEBUG(errs() << "use PHI " << *phi << "\n");
    }

    else if( BinaryOperator *binop = dyn_cast< BinaryOperator >(inst) )
    {
      reductionOpcode = binop->getOpcode();
      Reduction::Type rt0 = Reduction::isAssocAndCommut(binop);
      if( rt0 == Reduction::NotReduction )
      {
        DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 5 because used by non-assoc/-commut: " << *binop << '\n');
        return false;
      }

      if( rt0 != rt && rt != Reduction::NotReduction )
      {
        DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 6 because inconcistent reduction type\n");
        return false;
      }

      rt = rt0;
      u_binops.insert(binop);

      // TODO: these binops are only used by the reduction.
    }

    else if( CmpInst *cmp = dyn_cast< CmpInst >( inst ) )
    {
      BranchInst *br = dyn_cast< BranchInst >( * cmp->user_begin() );
      if( !br )
        return NotReduction;

      Reduction::Type rt0 = Reduction::isAssocAndCommut(cmp);
      if( rt0 == Reduction::NotReduction )
      {
        DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 5 because used by non-assoc/-commut: " << *cmp<< '\n');
        return false;
      }

      if( rt0 != rt && rt != Reduction::NotReduction )
      {
        DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 6 because inconcistent reduction type\n");
        return false;
      }

      rt = rt0;
      u_cmps.insert(cmp);
      u_brs.insert(br);
    }

    else
    {
      DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 7 because used* by " << *inst << '\n');
      return false;
    }

    // Add successors
    for(Value::use_iterator i=inst->use_begin(), e=inst->use_end(); i!=e; ++i)
    {
      //User *use = &**i;
      User *use = i->getUser();
      if( Instruction *iuse = dyn_cast< Instruction >(use) )
        fringe.push_back(iuse);
    }
  }

  // Reverse search: find all defs within the loop.
  fringe.push_back(phi0);
  VSet d_phis, d_binops, d_cmps, d_brs;
  while( !fringe.empty() )
  {
    Instruction *inst = fringe.back();
    fringe.pop_back();

    if( !loop->contains(inst) )
      continue;
    if( d_phis.count(inst) || d_binops.count(inst) || d_cmps.count(inst) ||
        d_brs.count(inst ))
      continue;

    if( PHINode *phi = dyn_cast< PHINode >(inst) )
    {
      d_phis.insert(phi);

      //DEBUG(errs() << "def PHI " << *phi << "\n");

      // add predecessors.
      for(unsigned i=0, N=phi->getNumIncomingValues(); i<N; ++i)
      {
        BasicBlock *bb = phi->getIncomingBlock(i);
        if( !loop->contains(bb) )
          continue;

        Value *v = phi->getIncomingValue(i);
        Instruction *def = dyn_cast< Instruction >(v);
        if( !def )
        {
          //sot: causing false negatives in ks
          DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 8 because it has a non-instruction def from inside the loop\n");
          return false;
        }

        // In a min-max reduction, when the current phi is not the head
        // phi(phi0), it is possible to have non-cmp / non-phi defs, because
        // if (val > max)
        //   max = val;
        // This means right after this code, in LLVM IR, max will be a phinode
        // between the old 'val' and the current 'val', and the old 'val' can
        // be any instruction (most likely a load, when the value is loaded
        // from memory)
        // In this case, we shouldn't add this instruction to the reduction set
        if (phi != phi0 && Max_i8 <= rt && rt <= Min_f64 &&
            (!isa< PHINode >(def) && !isa< CmpInst >(def)))
          continue;

        fringe.push_back(def);
      }
    }

    else if( isa< BinaryOperator >(inst) || isa< CmpInst >(inst) )
    {
      BinaryOperator *binop = dyn_cast<BinaryOperator>(inst);
      CmpInst *cmp = dyn_cast<CmpInst>(inst);
      BranchInst *br;
      if (cmp) {
        br = dyn_cast< BranchInst >( * cmp->user_begin() );
        if( !br )
          return NotReduction;
      }

      Reduction::Type rt0 = binop ? Reduction::isAssocAndCommut(binop) :
                                    Reduction::isAssocAndCommut(cmp);
      if( rt0 == Reduction::NotReduction )
      {
        DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 9 because defined by non-assoc/-commut: " << *binop << '\n');
        return false;
      }

      if( rt0 != rt )
      {
        DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 10 because inconcistent reduction type\n");
        return false;
      }

      // One, but not BOTH operands of this binary operator must be among the uses.
      Value *op0 = inst->getOperand(0),
            *op1 = inst->getOperand(1);
      const bool u0_bin = u_phis.count( op0 ) || u_binops.count( op0 ),
                 u1_bin = u_phis.count( op1 ) || u_binops.count( op1 ),
                 u0_cmp = u_phis.count( op0 ) || u_cmps.count( op0 ),
                 u1_cmp = u_phis.count( op1 ) || u_cmps.count( op1 );
      if( (u0_bin && u1_bin) || (u0_cmp && u1_cmp) )
      {
        DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 11 because BOTH operands of binop are derived from phi:\n"
                     << "  o op1: " << *op0 << '\n'
                     << "  o op2: " << *op1 << '\n');
        return false;
      }
      else if( (!u0_bin && !u1_bin) || (!u0_cmp && !u1_cmp) )
      {
        DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 12 because NEITHER operand of binop is derived from phi:\n"
                     << "  o op1: " << *op0 << '\n'
                     << "  o op2: " << *op1 << '\n');
        return false;
      }

      if (binop)
        d_binops.insert(binop);
      if (cmp) {
        d_cmps.insert(cmp);
        d_brs.insert(br);
      }

      if( u0_bin || u0_cmp )
        if( Instruction *i0 = dyn_cast< Instruction >( op0 ) )
          fringe.push_back(i0);
      if( u1_bin || u1_cmp )
        if( Instruction *i1 = dyn_cast< Instruction >( op1 ) )
          fringe.push_back(i1);

      // TODO: the other operand of the binop is does not depend on this PHI.
    }

    else
    {
      //sot: causing false negatives in ks
      DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 13 because defined by " << *inst << '\n');
      return false;
    }
  }

  // The sets must match: (u_phis == d_phis) and (u_binops == d_binops)

  //sot: causing false negatives in ks
  if( !(u_phis == d_phis) )
  {
    DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 14; PHI use/def mismatch\n");
    return false;
  }

  if( !(u_binops == d_binops) )
  {
    DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 15; binop use/def mismatch\n");
    return false;
  }
  // FIXME
  /*
  if( !(u_cmps == d_cmps) )
  {
    DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 16; cmp use/def mismatch\n");
    return false;
  }
  if( !(u_brs == d_brs) )
  {
    DEBUG(errs() << "PHI " << *phi0 << " is not a reduction 17; br use/def mismatch\n");
    return false;
  }*/

  //sot: causing false negatives in ks
  if( u_binops.empty() && u_cmps.empty() )
  {
    DEBUG(errs() <<"PHI " << *phi0 << " is not a reduction 18; neither binops nor cmps\n");
    return false;
  }

  if( !u_binops.empty() && !u_cmps.empty() )
  {
    DEBUG(errs() <<"PHI " << *phi0 << " is not a reduction 19; both binops and cmps\n");
    return false;
  }

  // All instructions in the reduction cycle
  VSet all, liveOuts;
  all.insert( u_phis.begin(), u_phis.end() );
  all.insert( u_binops.begin(), u_binops.end() );
  // u_cmps does not need to handled here, because cmp instructions are not
  // meant to be used from the outside

  // Determine the subset of the cycle which have uses
  // by an operation after the loop.
  for(VSet::iterator i=all.begin(), e=all.end(); i!=e; ++i)
  {
    Value *v = *i;
    for(Value::use_iterator j=v->use_begin(), z=v->use_end(); j!=z; ++j)
    {
      Use &u = *j;
      Instruction *user = dyn_cast< Instruction >( u.getUser() );
      if( !user )
        continue; // not an instruction
      if( loop->contains( user->getParent() ) )
        continue; // use within the loop.

      // 'user' is an instruction after the loop which uses 'v'.
      used_outside.insert(v);

      // There are two classes of live-outs which we support:
      // (1) the BIG PHI
      if( v == phi0 )
      {
        // Okay.
      }
      else if( isa<PHINode>(v) )
      {
        // Okay: this reduction is within a nested loop
        // If the reduction operation itself is within a nested loop, this can be a
        // PHI node that merges the values before the nested loop and after the nested
        // loop.
      }
      else if( !isa<PHINode>(v) && isLastUpdate(v, u_binops, loop) )
      {
        // Okay.
      }
      else
      {
        DEBUG(
          errs() << "PHI " << *phi0 << " is not a reduction 16.2:\n"
                 << " its cycle includes v=" << *v << '\n'
                 << " which is used after the loop by " << *user << '\n'
                 << " but v is not a last update.\n";
        );
        return false;
      }

      break;
    }
  }

  //sot: causing false negatives in ks
  assert(rt != Reduction::NotReduction && "Returning true but type not reduction");

  return true;
}

bool Reduction::isLastUpdate(Value *v, VSet &u_binops, Loop *loop)
{
  Instruction *inst = dyn_cast< Instruction >(v);
  if( !inst )
    return true;

  BasicBlock *header = loop->getHeader();
  BasicBlock *start = inst->getParent();

  // DFS for all blocks reachable from 'start'
  typedef std::vector<BasicBlock*> Fringe;
  Fringe fringe;
  VSet visited;
  fringe.push_back(start);
  while( !fringe.empty() )
  {
    BasicBlock *bb = fringe.back();
    fringe.pop_back();
    if( visited.count(bb) )
      continue;
    visited.insert(bb);

    // Is there an update in this block?
    bool haveSeenV = false;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *up = &*j;
      if( u_binops.count(up) )
      {
        // This block contains an update 'up'
        // (In the start block, only consider updates *after* inst
        if( up->getParent() != start || haveSeenV )
        {
          // We have found a path from 'inst' to another update 'up'
          return false;
        }

        if( up == inst )
          haveSeenV = true;
      }
    }

    // Add successors to fringe, stopping
    TerminatorInst *term = bb->getTerminator();
    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      BasicBlock *succ = term->getSuccessor(sn);

      // Is it a loop backedge?
      if( succ == header )
        continue;

      // Is it a loop exit?
      if( !loop->contains(succ) )
        continue; // yes.

      fringe.push_back( succ );
    }
  }

  return true;
}

/// Determine if accumulator is only accessed by reductions(ty) in loop.
bool Reduction::allOtherAccessesAreReduction(Loop *loop, Reduction::Type ty, Value *accumulator, LoopAA *loopaa)
{
  const DataLayout &td = *loopaa->getDataLayout();
  PointerType *pty = cast< PointerType >( accumulator->getType() );
  unsigned size_acc = td.getTypeStoreSize( pty->getElementType() );

  // Foreach operation in this loop which may access memory
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      if( ! inst->mayReadOrWriteMemory() )
        continue;

      // either:
      //   a. the operation is a reduction operation of the same type, or
      //   b. the operation does not access this accumulator.

      Value *target = 0;
      if( LoadInst *load = dyn_cast< LoadInst >(inst) )
      {
        // Condition a: the operation is a redux op of the same type
        Reduction::Type ty2 = Reduction::isReductionLoad(load);
        if( ty2 && ty2 == ty )
          continue; // Is a reduction of the same type
        target = load->getPointerOperand();
      }

      else if( StoreInst *store = dyn_cast< StoreInst >(inst) )
      {
        // Condition a: the operation is a redux op of the same type
        Reduction::Type ty2 = Reduction::isReductionStore(store);
        if( ty2 && ty2 == ty )
          continue; // Is a reduction of the same type
        target = store->getPointerOperand();
      }

      else
        // Not going to mess with mem-transfer-intrinsics nor callsites.
        //return false;
        continue; // FIXME maybe not safe, but inevitable

      // Condition b: the operation does not access this accumulator.
      PointerType *pty2 = cast< PointerType >( target->getType() );
      unsigned size_target = td.getTypeStoreSize( pty2->getElementType() );
      if( loopaa->alias(accumulator, size_acc, LoopAA::Same, target, size_target, loop) != LoopAA::NoAlias )
        return false;
      if( loopaa->alias(accumulator, size_acc, LoopAA::Before, target, size_target, loop) != LoopAA::NoAlias )
        return false;
      if( loopaa->alias(accumulator, size_acc, LoopAA::After, target, size_target, loop) != LoopAA::NoAlias )
        return false;
    }
  }
  return true;
}

/// Demote register reduxen to memory; canonicalize memory reduxen.
struct ReduxPass : public ModulePass
{
  static char ID;
  ReduxPass() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< ModuleLoops >();
    //au.addRequired< DominatorTreeWrapperPass >();
    //au.addRequired< LoopInfoWrapperPass >();
    //au.addRequired< ScalarEvolutionWrapperPass >();
    //au.addRequired< LoopAA >();
  }

  bool runOnModule(Module &mod)
  {
    bool modified = false;

    for(Module::iterator i=mod.begin(), e=mod.end(); i!=e; ++i)
      if( ! i->isDeclaration() )
        modified |= runOnFunction(*i);

    return modified;
  }

  bool runOnFunction(Function &fcn)
  {
    if( TargetFcn != ""
    &&  TargetFcn != fcn.getName() )
      return false;

    DEBUG(errs() << "SpecPriv Redux: Processing function "
      << fcn.getName() << ":\n");

    // Collect all loops
    ModuleLoops &mloops = getAnalysis<ModuleLoops>();
    DominatorTree &dt = mloops.getAnalysis_DominatorTree(&fcn);
    //DominatorTree &dt = getAnalysis<DominatorTreeWrapperPass>(fcn).getDomTree();
    LoopInfo &li = mloops.getAnalysis_LoopInfo(&fcn);
    //LoopInfo &li = getAnalysis<LoopInfoWrapperPass>(fcn).getLoopInfo();
    std::vector<Loop*> all_loops( li.begin(), li.end() );
    for(unsigned i=0; i<all_loops.size(); ++i)
    {
      Loop *loop = all_loops[i];

      all_loops.insert(all_loops.end(),
        loop->getSubLoops().begin(),
        loop->getSubLoops().end());

      if( TargetLoop != ""
      &&  TargetLoop != loop->getHeader()->getName() )
        continue;
    }

    if( all_loops.empty() )
      return false;

    bool modified = false;

    // process all loops, outermost -> innermost
     ScalarEvolution &scev = mloops.getAnalysis_ScalarEvolution(&fcn);
    //ScalarEvolution &scev = getAnalysis<ScalarEvolutionWrapperPass>(fcn).getSE();

    for(unsigned i=0, N=all_loops.size(); i<N; ++i)
      modified |= demoteRegisterReductions(dt, li, scev, all_loops[i]);

/*
    // process all loops, innermost -> outermost
    std::set<BasicBlock*> subloops;
    std::map<BasicBlock*,InstInsertPt> points;
    for(int i=all_loops.size()-1; i>=0; --i)
      modified |= canonicalizeMemoryReductions(all_loops[i], subloops, points);
*/

    mloops.forget(&fcn);

    if( VerifyEachFcn && modified )
      verifyFunction(fcn);

    return modified;
  }

private:
  /// Analyze and transform subloop reductions.
  bool canonicalizeMemoryReductions(Loop *loop, std::set<BasicBlock*> &subloops, std::map<BasicBlock*,InstInsertPt> &points)
  {
    bool modified = false;

    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    DEBUG(errs() << "SpecPriv Redux: Canonicalizeing memory reductions in loop "
      << fcn->getName() << ":" << header->getName() << "\n");

    LoopAA *loopaa = getAnalysis< LoopAA >().getTopAA();
    std::set<Value*> already;

    // For each basic block that is within this loop,
    // but which is not within a subloop...
    for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    {
      BasicBlock *bb = *i;
      if( subloops.count(bb) )
        continue;

      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      {
        LoadInst *load = dyn_cast< LoadInst >(&*j);
        if( !load )
          continue;

        Value *accumulator = load->getPointerOperand();
        if( already.count(accumulator) )
          continue;
        already.insert(accumulator);

        // Pointer to accumulator must be loop invariant
        if( !loop->isLoopInvariant(accumulator) )
          continue;

        const BinaryOperator *add=0;
        const StoreInst *store=0;
        const CmpInst *cmp=0;
        const BranchInst *br=0;

        Reduction::Type t = Reduction::isReductionLoad(load,&add,&cmp,&br,&store);
        if( !t )
          continue;

        // Looks like a reduction.
        // Next, we will use static analysis to ensure that
        //  for every other memory operation in this loop, either:
        //   a. the operation is a reduction operation of the same type, or
        //   b. the operation does not access this accumulator.

        if( !Reduction::allOtherAccessesAreReduction(loop, t, accumulator, loopaa) )
          continue;

        // Cool.

        Value *new_accum = canonicalizeMemoryReduction(t, loop, accumulator, points);
        already.insert(new_accum);
        modified = true;
        ++numCanon;
      }

      subloops.insert(bb);
    }
    return modified;
  }

  /// Transform a subloop reduction
  Value *canonicalizeMemoryReduction(Reduction::Type t, Loop *loop, Value *accumulator, std::map<BasicBlock*,InstInsertPt> &points)
  {
    BasicBlock *header = loop->getHeader();
    BasicBlock *preheader = loop->getLoopPreheader();
    assert( preheader && "did you run loop simplify?");
    Function *fcn = preheader->getParent();

    // Get Storage
    PointerType *pty = cast<PointerType>( accumulator->getType() );
    Type *accty = pty->getElementType();

    AllocaInst *alloca = new AllocaInst(accty, 0,
      accumulator->getName() + ".subloop-redux." + header->getName());
    InstInsertPt::Beginning(fcn) << alloca;

    LLVMContext &ctx = fcn->getContext();

    // Initialize Storage before loop
    Value *identity = Reduction::getIdentityValue(t, ctx);
    InstInsertPt::End(preheader) << new StoreInst(identity, alloca);

    // Replace accumulator within loop.
    for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    {
      BasicBlock *bb = *i;
      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;
        inst->replaceUsesOfWith(accumulator,alloca);
      }
    }

    // Update outer-reduction after loop.
    // foreach loop exit
    llvm::SmallVector<BasicBlock*,4> exits;
    loop->getExitBlocks(exits);
    for(unsigned i=0, N=exits.size(); i<N; ++i)
    {
      BasicBlock *exit = exits[i];
      if( !points.count( exit ) )
        points[ exit ] = InstInsertPt::Beginning(exit);

      InstInsertPt &where = points[exit];

      LoadInst *load_inner = new LoadInst(alloca);
      LoadInst *load_outer = new LoadInst(accumulator);
      where << load_inner  << load_outer;

      switch( t )
      {
        case Reduction::Add_i8:
        case Reduction::Add_i16:
        case Reduction::Add_i32:
        case Reduction::Add_i64:
          {
            BinaryOperator *binop = BinaryOperator::Create(BinaryOperator::Add, load_outer,load_inner);
            StoreInst *store = new StoreInst(binop,  accumulator);
            where << binop << store;
          }
          break;

        case Reduction::Add_f32:
        case Reduction::Add_f64:
          {
            BinaryOperator *binop = BinaryOperator::Create(BinaryOperator::FAdd, load_outer,load_inner);
            StoreInst *store = new StoreInst(binop,  accumulator);
            where << binop << store;
          }
          break;

        // TODO: min, max reductions
        case Reduction::Min_f32:
        case Reduction::Min_f64:
        case Reduction::Max_f32:
        case Reduction::Max_f64:

        case Reduction::Min_u8:
        case Reduction::Min_u16:
        case Reduction::Min_u32:
        case Reduction::Min_u64:
        case Reduction::Max_u8:
        case Reduction::Max_u16:
        case Reduction::Max_u32:
        case Reduction::Max_u64:

        case Reduction::Min_i8:
        case Reduction::Min_i16:
        case Reduction::Min_i32:
        case Reduction::Min_i64:
        case Reduction::Max_i8:
        case Reduction::Max_i16:
        case Reduction::Max_i32:
        case Reduction::Max_i64:

        case Reduction::NotReduction:
          assert(false && "Not yet implemented");
      }
    }

    DEBUG(errs() << "- Accumulator " << *accumulator
      << "=> " << *alloca << '\n');
    return alloca;
  }

#undef DEBUG_TYPE
#define DEBUG_TYPE "specpriv-reduction-transform"

  /// TODO: clean this up, generalize more (?), and move to the Utils directory.
  /// At every use of 'used':
  //    - yield to the callback cb(used, where).
  //    - 'where' is an InstInsertPt saying where to generate the code.
  //    - Then replace said use of 'used' with the return value of cb.
  //  Optionally, update loop info 'li' and dominator trees 'dt'.
  //  Optionally, only replace those uses that occur OUTSIDE of the loop 'only_instructions_outside_of'
  template <class Callback>
  static void replaceUsesWithGeneratedCode(Value *used, Callback &cb, Loop *only_instructions_outside_of = 0, StringRef prefix = "redux.remat.edge.", LoopInfo *li=0, DominatorTree *dt=0)
  {
    // capture a collection of all uses.
    std::vector<Value*> users( used->user_begin(), used->user_end() );
    for(unsigned i=0, I=users.size(); i<I; ++i)
    {
      // Each use is: a PHI Node, an instruction, or something else.
      if( PHINode *phi = dyn_cast< PHINode >( users[i] ) )
      {
        BasicBlock *usebb = phi->getParent();
        if( only_instructions_outside_of )
          if( only_instructions_outside_of->contains(phi) )
            continue;

        Function *fcn = usebb->getParent();
        LLVMContext &ctx = fcn->getContext();

        Loop *lsucc = 0;
        if( li )
          lsucc = li->getLoopFor( usebb );

        for(unsigned pn=0, PN=phi->getNumIncomingValues(); pn<PN; ++pn)
        {
          if( phi->getIncomingValue(pn) != used )
            continue;

          BasicBlock *pred = phi->getIncomingBlock(pn);
          Loop *lpred = 0;
          if( li )
            lpred = li->getLoopFor( pred );

          BasicBlock *remat_block = BasicBlock::Create(
            ctx, Twine(prefix) + pred->getName(), fcn);
          remat_block->moveAfter(pred);

          BranchInst *br = BranchInst::Create(usebb, remat_block);
          InstInsertPt where = InstInsertPt::Before(br);

          // Replace the *FIRST* use of 'usebb' in term
          TerminatorInst *term = pred->getTerminator();
          for(unsigned sn=0, SN=term->getNumSuccessors(); sn<SN; ++sn)
            if( term->getSuccessor(sn) == usebb )
            {
              term->setSuccessor(sn, remat_block);
              break;
            }

          // Update the PHIs in 'usebb' so that their incoming
          // values come from 'remat_block' instead of 'pred'
          for(BasicBlock::iterator i=usebb->begin(), e=usebb->end(); i!=e; ++i)
          {
            PHINode *phix = dyn_cast<PHINode>( &*i );
            if( !phix )
              break;

            for(unsigned pnx=0, PNx=phix->getNumIncomingValues(); pnx<PNx; ++pnx)
              if( pred == phix->getIncomingBlock(pnx) )
              {
                phix->setIncomingBlock(pnx, remat_block);

                // Replace only the *FIRST* occurrence of 'pred' in each
                // PHI's incoming block list.  If it appears more than once,
                // then it will also appear more than once in the terminator
                // 'term' successor list, and thus several times in 'users'.
                // Trust me: only the first!
                break;
              }
          }

          // Update LoopInfo, if provided
          if( li && lpred && lsucc )
          {
            Loop *target = lpred;
            if( lsucc->getLoopDepth() < lpred->getLoopDepth() )
              target = lsucc;

            //sot
            LoopInfoBase<BasicBlock, Loop> &lib = *li;
            target->addBasicBlockToLoop(remat_block, lib);
          }

          // Update DominatorTree, if provided
          if( dt )
          {
            dt->addNewBlock(remat_block, pred);
            if( dt->getNode(usebb)->getIDom() == dt->getNode(pred) )
            {
              dt->changeImmediateDominator(usebb, remat_block);
              dt->changeImmediateDominator(remat_block, pred);
            }
          }

          Value *replacement = cb(used, where);

          // Replace use
          phi->setIncomingValue(pn, replacement);
        }
      }

      else if( Instruction *user = dyn_cast< Instruction >( users[i] ) )
      {
        BasicBlock *usebb = user->getParent();
        if( only_instructions_outside_of )
          if( only_instructions_outside_of->contains(usebb) )
            continue;

        InstInsertPt where = InstInsertPt::Before(user);
        Value *replacement = cb(used, where);
        user->replaceUsesOfWith(used, replacement);
      }
    }
  }

  struct RematerializeReduction
  {
    RematerializeReduction(PHINode *_phi0, BinaryOperator::BinaryOps opcode, AllocaInst *_mphi, AllocaInst *_rphi)
      : phi0(_phi0), reductionOpcode(opcode), mphi(_mphi), rphi(_rphi) {}

    PHINode *phi0;
    BinaryOperator::BinaryOps reductionOpcode;
    AllocaInst *mphi, *rphi;

    Value *operator()(Value *used_value, InstInsertPt &where)
    {
      // Rematerialize
      // 'used_value' refers to either the big phi, or to the last update.
      if( used_value == phi0 )
      {
        // It uses the big phi.  Replace it with mphi
        LoadInst *loadm = new LoadInst(mphi, "load.M");

        where << loadm;
        return loadm;
      }

      assert( isa< BinaryOperator >(used_value) );

      // It uses the last update.  Replace it with the sum of mphi and rphi
      LoadInst *loadm = new LoadInst(mphi, "load.M");
      LoadInst *loadr = new LoadInst(rphi, "load.R");
      BinaryOperator *binop = BinaryOperator::Create(
        reductionOpcode, loadm, loadr, "redux.remat.binop.");
      ++numRemat;

      where << loadm << loadr << binop;
      return binop;
    }
  };


  bool demoteRegisterReduction(DominatorTree &dt, LoopInfo &li, ScalarEvolution &scev, Loop *loop, PHINode *phi0, std::set<PHINode*> &ignore)
  {
    Value *initial_value = 0;
    VSet u_phis, u_binops, u_cmps, u_brs, used_outside;
    Reduction::Type rt = Reduction::NotReduction;
    BinaryOperator::BinaryOps reductionOpcode;
    if( ! Reduction::isRegisterReduction(
        scev, loop, phi0, nullptr, ignore,
        rt, reductionOpcode, u_phis, u_binops, u_cmps, u_brs, used_outside,
        initial_value) )
    {
      return false;
    }

    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    DEBUG(errs() << "\nSpecPriv Redux: Processing register reductions in loop "
      << fcn->getName() << ":" << header->getName() << "\n");

    DEBUG(
      errs() << "Found a register reduction:\n"
             << "          PHI: " << *phi0 << '\n'
             << "      Initial: " << *initial_value << '\n'
             << "    Internals:\n";
      for(VSet::iterator i=u_phis.begin(), e=u_phis.end(); i!=e;  ++i)
        errs() << "            o " << **i << '\n';
      errs() << "      Updates:\n";
      for(VSet::iterator i=u_binops.begin(), e=u_binops.end(); i!=e;  ++i)
        errs() << "            o " << **i << '\n';

      errs() << "    Live-outs:\n";
      for(VSet::iterator i=used_outside.begin(), e=used_outside.end(); i!=e;  ++i)
        errs() << "            o " << **i << '\n';
    );

    // Update.
    AllocaInst *mphi = new AllocaInst(initial_value->getType(), 0, "reg_redux_M_phi");
    AllocaInst *rphi = new AllocaInst(initial_value->getType(), 0, "reg_redux_R_phi");
    InstInsertPt::Beginning(fcn) << mphi << rphi;

    DEBUG(
      errs() << "    New PHI Nodes:\n"
             << "            o " << mphi->getName() << '\n'
             << "            o " << rphi->getName() << '\n');

    // Set initial value.
    BasicBlock *preheader = loop->getLoopPreheader();
    assert( preheader && "Need preheader; run loop simplify");
    InstInsertPt::End(preheader) << new StoreInst(initial_value, mphi);

    LLVMContext &ctx = fcn->getContext();

    // Reset rphi in header
    InstInsertPt::Beginning(header)
      << new StoreInst(Reduction::getIdentityValue(rt, ctx), rphi);

    // Each update should load/store
    for(VSet::iterator i=u_binops.begin(), e=u_binops.end(); i!=e; ++i)
    {
      BinaryOperator *binop = cast< BinaryOperator >(*i);

      Value *op0 = binop->getOperand(0),
            *op1 = binop->getOperand(1);
      const bool u0 = u_phis.count( op0 ) || u_binops.count( op0 );

      Instruction *load = new LoadInst(rphi);
      InstInsertPt::Before(binop) << load;

      if( u0 )
        // Operand 0 is the accumulator
        binop->replaceUsesOfWith(op0, load);

      else
        // Operand 1 is the accumulator.
        binop->replaceUsesOfWith(op1, load);

      Instruction *store = new StoreInst(binop, rphi);
      InstInsertPt::After(binop) << store;
    }

    // Split loop backedges to accumulate rphi into mphi
    for(unsigned pn=0, N=phi0->getNumIncomingValues(); pn<N; ++pn)
    {
      BasicBlock *pred = phi0->getIncomingBlock(pn);
      if( ! loop->contains(pred) )
        continue; // loop entry

      // pred->header is a loop backedge.
      BasicBlock *split_backedge = split(pred,header, dt, "redux.backedge.");

      //sot
      //loop->addBasicBlockToLoop(split_backedge, li.getBase());
      loop->addBasicBlockToLoop(split_backedge, li);

      LoadInst *loadr = new LoadInst(rphi, "load.R");
      LoadInst *loadm = new LoadInst(mphi, "load.M");
      BinaryOperator *binop = BinaryOperator::Create(
        reductionOpcode, loadm, loadr, "redux.accumulate.r.into.m.");
      StoreInst *storem = new StoreInst(binop, mphi);

      InstInsertPt::End(split_backedge)
        << loadr
        << loadm
        << binop
        << storem;
    }

    // Replace live-out with loads.
    RematerializeReduction remat(phi0, reductionOpcode, mphi, rphi);

    for(VSet::iterator i=used_outside.begin(), e=used_outside.end(); i!=e; ++i)
    {
      Instruction *inst = cast< Instruction >( *i );
      replaceUsesWithGeneratedCode(inst, remat, loop, "remat.redux.edge.", &li, &dt);
    }

    // Remove the phis.
    for(VSet::iterator i=u_phis.begin(), e=u_phis.end(); i!=e; ++i)
    {
      PHINode *phi = cast< PHINode >( *i );
      phi->dropAllReferences();
    }
    for(VSet::iterator i=u_phis.begin(), e=u_phis.end(); i!=e; ++i)
    {
      PHINode *phi = cast< PHINode >( *i );
      phi->eraseFromParent();
    }

    // Promote rphi from mem to reg.
    if( isAllocaPromotable(rphi) )
    {
      std::vector<AllocaInst*> singleton;
      singleton.push_back(rphi);
      PromoteMemToReg(singleton, dt);
    }

    scev.forgetLoop(loop);
    ++numRedux;
    return true;
  }


  bool demoteRegisterReductions(DominatorTree &dt, LoopInfo &li, ScalarEvolution &scev, Loop *loop)
  {
    BasicBlock *header = loop->getHeader();

    bool modified = false;

    std::set<PHINode*> ignore;
    for(;;)
    {
      bool imod = false;

      for(BasicBlock::iterator i=header->begin(), e=header->end(); i!=e; ++i)
      {
        PHINode *phi = dyn_cast< PHINode >( &*i );
        if( !phi )
          break;

        imod = demoteRegisterReduction(dt, li, scev, loop,phi, ignore); // || demotePrivateRegister(li, loop,phi, ignore);
        if( imod )
          break;
      }

      if( !imod )
        break;

      modified |= imod;
    }

    return modified;
  }

};

char ReduxPass::ID = 0;
static RegisterPass<ReduxPass> x("spec-priv-reduction",
  "Demote reductions in registers -> reductions in memory");

StringRef Reduction::names[] =
  { "no_redux",
    "i8_add", "i16_add", "i32_add", "i64_add",
    "f32_add", "f64_add",
    "i8_max", "i16_max", "i32_max", "i64_max",
    "u8_max", "u16_max", "u32_max", "u64_max",
    "f32_max", "f64_max",
    "i8_min", "i16_min", "i32_min", "i64_min",
    "u8_min", "u16_min", "u32_min", "u64_min",
    "f32_min", "f64_min"
  };
}
}

