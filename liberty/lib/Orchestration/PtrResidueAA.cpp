#define DEBUG_TYPE "spec-priv-ptr-residue-aa"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

#include "liberty/Orchestration/PtrResidueAA.h"
#include "liberty/Orchestration/PtrResidueRemed.h"

#ifndef DEFAULT_PTR_RESIDUE_REMED_COST
#define DEFAULT_PTR_RESIDUE_REMED_COST 60
#endif

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numQueries, "Num queries received");
STATISTIC(numApplicable, "Num applicable queries");
STATISTIC(numNoAlias, "Num no-alias / no-modref");
STATISTIC(numBeneChecks, "Num times we had to check for benefit");
STATISTIC(numBenefit, "Num no-alias / no-modref which require speculation");

// Rotate bv to the left by N bits
static uint16_t rol_i16(uint16_t bv, unsigned N)
{
  return (bv << N) | (bv >> (16-N));
}

// Signed rotation: if N>0, rotate left; otherwise, rotate right.
static uint16_t rotate_i16(uint16_t bv, int N=1)
{
  N %= 16;
  if( N < 0 )
    N += 16;
  return rol_i16(bv,(unsigned)N);
}

// The bit-vector captures the distinct addresses (mod 16)
// which the pointer points-to.
// Memory accesses are typically > 1 byte in size.
// This spreads that bit-vector over the size of the
// access to compute the footprint (mod 16)
static uint16_t widen(uint16_t bv, unsigned access_size_bytes)
{
  if( 16 <= access_size_bytes )
    access_size_bytes = 16;

  uint16_t accum = 0;
  for(unsigned i=0; i<access_size_bytes && accum != 0x0ffffu; ++i)
    accum = bv | rotate_i16(accum);

  return accum;
}

// Residues are independent?
static bool residues_overlap(uint16_t bv1, unsigned size1, int correction1, uint16_t bv2, unsigned size2, int correction2)
{
  const uint16_t adjusted1 = rotate_i16(bv1, correction1);
  const uint16_t adjusted2 = rotate_i16(bv2, correction2);

  return 0 != (widen(adjusted1,size1) & widen(adjusted2,size2));
}

static int computeConstantOffset(const DataLayout &td, const GetElementPtrInst *gep)
{
  assert( gep->hasAllConstantIndices() );

  gep_type_iterator gi=gep_type_begin(gep), ge=gep_type_end(gep);
  GetElementPtrInst::const_op_iterator oi=gep->idx_begin();

  int accum = 0;
  for(; gi!=ge; ++gi, ++oi)
  {
    ConstantInt *ci = cast< ConstantInt >(*oi);
    int lv = ci->getSExtValue();

    accum += lv * td.getTypeAllocSize(gi.getIndexedType());
  }
  LLVM_DEBUG(errs() << "In ``" << *gep << "'': result-base == " << accum << " bytes\n");
  return accum;
}

static const Value *adjust_pointer(const Value *v, const DataLayout &td, /* output: */ int &correction)
{
  for(;;)
  {
    if( const CastInst *cast = dyn_cast<CastInst>(v) )
    {
      v = cast->getOperand(0);
      continue;
    }

    else if( const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(v) )
    {
      if( gep->hasAllConstantIndices() )
      {
        v = gep->getPointerOperand();
        correction += computeConstantOffset(td,gep);
        continue;
      }
    }

    return v;
  }
}

bool PtrResidueAA::may_alias(
    const Value *P1, unsigned size1,
    TemporalRelation rel,
    const Value *P2, unsigned size2,
    const Loop *L,
    PtrResidueSpeculationManager::Assumption &ass1_out,
    PtrResidueSpeculationManager::Assumption &ass2_out) const
{
  if( !L )
    return true;

  if( ! P1->getType()->isPointerTy() )
    return true;
  if( ! P2->getType()->isPointerTy() )
    return true;

  if( P1 == P2 )
    return true;

  ++numApplicable;

  int correction1 = 0;
  const Value *ptr1 = adjust_pointer(P1, td, correction1);

  int correction2 = 0;
  const Value *ptr2 = adjust_pointer(P2, td, correction2);

  const Read &read = manager.getSpecPrivResult();
  const Ctx *ctx = read.getCtx(L);

  LLVM_DEBUG(errs() << "ptr-residue-aa(" << *P1 << ", " << *P2 << ")\n");

  const uint16_t residual1 = read.getPointerResiduals(ptr1, ctx);
  LLVM_DEBUG(errs() << " residue(" << *ptr1 << ") = " << residual1 << '\n');
  if( residual1 == 0 || residual1 == 0x0ffffu )
    return true;

  const uint16_t residual2 = read.getPointerResiduals(ptr2, ctx);
  LLVM_DEBUG(errs() << " residue(" << *ptr2 << ") = " << residual2 << '\n');
  if( residual2 == 0 || residual2 == 0x0ffffu )
    return true;

  LLVM_DEBUG(
    errs() << " Pointer ``" << *ptr1 << "'' has residuals " << residual1 << ", correction " << correction1 << '\n';
    errs() << " Pointer ``" << *ptr2 << "'' has residuals " << residual2 << ", correction " << correction2 << '\n';
  );

  if( residues_overlap( residual1,size1,correction1, residual2,size2,correction2) )
    return true;

  ass1_out = PtrResidueSpeculationManager::Assumption(ptr1,ctx);
  ass2_out = PtrResidueSpeculationManager::Assumption(ptr2,ctx);

  LLVM_DEBUG(errs() << " --> NoAlias\n");
  ++numNoAlias;
  return false;
}

bool PtrResidueAA::may_modref(
  const Instruction *A,
  TemporalRelation rel,
  const Value *P2, unsigned S2,
  const Loop *L,
  PtrResidueSpeculationManager::Assumption &ass_out1,
  PtrResidueSpeculationManager::Assumption &ass_out2) const
{
  if( const StoreInst *store = dyn_cast<StoreInst>(A) )
  {
    const Value *P1 = store->getPointerOperand();
    unsigned S1 = td.getTypeStoreSize( store->getValueOperand()->getType() );

    if( !may_alias(P1,S1,rel,P2,S2,L,ass_out1,ass_out2) )
      return false;
  }
  else if( const LoadInst *load = dyn_cast<LoadInst>(A) )
  {
    const Value *P1 = load->getPointerOperand();
    unsigned S1 = td.getTypeStoreSize( load->getType() );

    if( !may_alias(P1,S1,rel,P2,S2,L,ass_out1,ass_out2) )
      return false;
  }

  return true;
}

LoopAA::AliasResult PtrResidueAA::alias(const Value *P1, unsigned S1,
                                        TemporalRelation rel, const Value *P2,
                                        unsigned S2, const Loop *L, Remedies &R,
                                        DesiredAliasResult dAliasRes) {
  ++numQueries;

  if (dAliasRes == DMustAlias)
    return LoopAA::alias(P1, S1, rel, P2, S2, L, R, dAliasRes);

  PtrResidueSpeculationManager::Assumption a1,a2;
  if( may_alias(P1,S1,rel,P2,S2,L,a1,a2) )
    return LoopAA::alias(P1,S1,rel,P2,S2,L,R); // no help

  // We can report no-alias.

  // Before adding assumptions, check if the rest of the stack would have
  // reported no-alias without this speculation.
  /*
  if( !manager.isAssumed(a1) || !manager.isAssumed(a2) )
  {
    ++numBeneChecks;
    if( NoAlias == LoopAA::alias(P1,S1,rel,P2,S2,L,R) )
      return NoAlias; // speculation was not necessary.
  }
  */

  // Speculation is required to report no-alias.
  // Record this requirement.
  manager.setAssumed(a1);
  manager.setAssumed(a2);
  ++numBenefit;

  std::shared_ptr<PtrResidueRemedy> remedy1 =
      std::shared_ptr<PtrResidueRemedy>(new PtrResidueRemedy());
  //remedy1->cost = DEFAULT_PTR_RESIDUE_REMED_COST;
  remedy1->ptr = a1.first;
  remedy1->ctx = a1.second;
  remedy1->setCost(perf, a1.first);
  R.insert(remedy1);

  std::shared_ptr<PtrResidueRemedy> remedy2 =
      std::shared_ptr<PtrResidueRemedy>(new PtrResidueRemedy());
  //remedy2->cost = DEFAULT_PTR_RESIDUE_REMED_COST;
  remedy2->ptr = a2.first;
  remedy2->ctx = a2.second;
  remedy2->setCost(perf, a2.first);
  R.insert(remedy2);

  return NoAlias;
}

LoopAA::ModRefResult PtrResidueAA::modref(
  const Instruction *A,
  TemporalRelation rel,
  const Value *P2, unsigned S2,
  const Loop *L, Remedies &R)
{
  ++numQueries;

  PtrResidueSpeculationManager::Assumption a1,a2;
  if( may_modref(A,rel,P2,S2,L,a1,a2) )
    return LoopAA::modref(A,rel,P2,S2,L,R); // no help

  // We can report no-mod-ref.

  /*
  // Before adding assumptions, check if the rest of the stack would have
  // reported no-mod-ref without this speculation.
  if( !manager.isAssumed(a1) || !manager.isAssumed(a2) )
  {
    ++numBeneChecks;
    if( NoModRef == LoopAA::modref(A,rel,P2,S2,L,R) )
      return NoModRef; // speculation was not necessary.
  }
  */

  // Speculation is required to report no-mod-ref.
  // Record this requirement.
  manager.setAssumed(a1);
  manager.setAssumed(a2);
  ++numBenefit;

  std::shared_ptr<PtrResidueRemedy> remedy1 =
      std::shared_ptr<PtrResidueRemedy>(new PtrResidueRemedy());
  //remedy1->cost = DEFAULT_PTR_RESIDUE_REMED_COST;
  remedy1->ptr = a1.first;
  remedy1->ctx = a1.second;
  remedy1->setCost(perf, a1.first);
  R.insert(remedy1);

  std::shared_ptr<PtrResidueRemedy> remedy2 =
      std::shared_ptr<PtrResidueRemedy>(new PtrResidueRemedy());
  //remedy2->cost = DEFAULT_PTR_RESIDUE_REMED_COST;
  remedy2->ptr = a2.first;
  remedy2->ctx = a2.second;
  remedy2->setCost(perf, a2.first);
  R.insert(remedy2);

  return NoModRef;
}

LoopAA::ModRefResult PtrResidueAA::modref(
  const Instruction *A,
  TemporalRelation rel,
  const Instruction *B,
  const Loop *L,
  Remedies &R)
{
  ++numQueries;

  PtrResidueSpeculationManager::Assumption a1,a2;
  const Value *P2=0;
  unsigned S2=0;
  if( const StoreInst *store = dyn_cast<StoreInst>(B) )
  {
    P2 = store->getPointerOperand();
    S2 = td.getTypeStoreSize( store->getValueOperand()->getType() );
  }

  else if( const LoadInst *load = dyn_cast<LoadInst>(B) )
  {
    P2 = load->getPointerOperand();
    S2 = td.getTypeStoreSize( load->getType() );
  }

  if( P2 )
  {
    if( may_modref(A,rel,P2,S2,L,a1,a2) )
      return LoopAA::modref(A,rel,B,L,R); // no help

    // We can report no-mod-ref.

    /*
    // Before adding assumptions, check if the rest of the stack would have
    // reported no-mod-ref without this speculation.
    if( !manager.isAssumed(a1) || !manager.isAssumed(a2) )
    {
      ++numBeneChecks;
      if( NoModRef == LoopAA::modref(A,rel,B,L,R) )
        return NoModRef; // speculation was not necessary.
    }
    */

    // Speculation is required to report no-mod-ref.
    // Record this requirement.
    manager.setAssumed(a1);
    manager.setAssumed(a2);
    ++numBenefit;

    std::shared_ptr<PtrResidueRemedy> remedy1 =
        std::shared_ptr<PtrResidueRemedy>(new PtrResidueRemedy());
    // remedy1->cost = DEFAULT_PTR_RESIDUE_REMED_COST;
    remedy1->ptr = a1.first;
    remedy1->ctx = a1.second;
    remedy1->setCost(perf, a1.first);
    R.insert(remedy1);

    std::shared_ptr<PtrResidueRemedy> remedy2 =
        std::shared_ptr<PtrResidueRemedy>(new PtrResidueRemedy());
    // remedy2->cost = DEFAULT_PTR_RESIDUE_REMED_COST;
    remedy2->ptr = a2.first;
    remedy2->ctx = a2.second;
    remedy2->setCost(perf, a2.first);
    R.insert(remedy2);

    return NoModRef;
  }

  // All other cases.
  return LoopAA::modref(A,rel,B,L,R);
}

}
}
