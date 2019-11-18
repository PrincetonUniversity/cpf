#define DEBUG_TYPE "ptr-residue-remed"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

#include "liberty/Orchestration/PtrResidueRemed.h"

#define DEFAULT_PTR_RESIDUE_REMED_COST 60

namespace liberty {
using namespace llvm;

STATISTIC(numApplicable, "Num applicable queries");
STATISTIC(numNoMemDep, "Number of mem deps removed by ptr residue");

void PtrResidueRemedy::apply(Task *task) {}

bool PtrResidueRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<PtrResidueRemedy> ptrResRhs =
      std::static_pointer_cast<PtrResidueRemedy>(rhs);
  if (this->ptr == ptrResRhs->ptr) {
    return this->ctx < ptrResRhs->ctx;
  }
  return this->ptr < ptrResRhs->ptr;
  /*
  if (this->ptr1 == ptrResRhs->ptr1) {
    if (this->ctx1 == ptrResRhs->ctx1) {
      if (this->ptr2 == ptrResRhs->ptr2) {
        return this->ctx2 < ptrResRhs->ctx2;
      }
      return this->ptr2 < ptrResRhs->ptr2;
    }
    return this->ctx1 < ptrResRhs->ctx1;
  }
  return this->ptr1 < ptrResRhs->ptr1;
  */
}

unsigned long PtrResidueRemedy::setCost(PerformanceEstimator *perf,
                                        const Value *ptr) {
  // 1 cmp, 1 bitwise, 1 branch
  unsigned validation_weight = 201;
  this->cost = 0;
  if (const Instruction *gravity = dyn_cast<Instruction>(ptr1))
    this->cost += Remediator::estimate_validation_weight(perf, gravity,
                                                         validation_weight);
}

// Rotate bv to the left by N bits
static uint16_t rol_i16(uint16_t bv, unsigned N) {
  return (bv << N) | (bv >> (16 - N));
}

// Signed rotation: if N>0, rotate left; otherwise, rotate right.
static uint16_t rotate_i16(uint16_t bv, int N = 1) {
  N %= 16;
  if (N < 0)
    N += 16;
  return rol_i16(bv, (unsigned)N);
}

// The bit-vector captures the distinct addresses (mod 16)
// which the pointer points-to.
// Memory accesses are typically > 1 byte in size.
// This spreads that bit-vector over the size of the
// access to compute the footprint (mod 16)
static uint16_t widen(uint16_t bv, unsigned access_size_bytes) {
  if (16 <= access_size_bytes)
    access_size_bytes = 16;

  uint16_t accum = 0;
  for (unsigned i = 0; i < access_size_bytes && accum != 0x0ffffu; ++i)
    accum = bv | rotate_i16(accum);

  return accum;
}

// Residues are independent?
static bool residues_overlap(uint16_t bv1, unsigned size1, int correction1,
                             uint16_t bv2, unsigned size2, int correction2) {
  const uint16_t adjusted1 = rotate_i16(bv1, correction1);
  const uint16_t adjusted2 = rotate_i16(bv2, correction2);

  return 0 != (widen(adjusted1, size1) & widen(adjusted2, size2));
}

static int computeConstantOffset(const DataLayout &td,
                                 const GetElementPtrInst *gep) {
  assert(gep->hasAllConstantIndices());

  gep_type_iterator gi = gep_type_begin(gep), ge = gep_type_end(gep);
  GetElementPtrInst::const_op_iterator oi = gep->idx_begin();

  int accum = 0;
  for (; gi != ge; ++gi, ++oi) {
    ConstantInt *ci = cast<ConstantInt>(*oi);
    int lv = ci->getSExtValue();

    accum += lv * td.getTypeAllocSize(gi.getIndexedType());
  }
  DEBUG(errs() << "In ``" << *gep << "'': result-base == " << accum
               << " bytes\n");
  return accum;
}

static const Value *adjust_pointer(const Value *v, const DataLayout &td,
                                   /* output: */ int &correction) {
  for (;;) {
    if (const CastInst *cast = dyn_cast<CastInst>(v)) {
      v = cast->getOperand(0);
      continue;
    }

    else if (const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(v)) {
      if (gep->hasAllConstantIndices()) {
        v = gep->getPointerOperand();
        correction += computeConstantOffset(td, gep);
        continue;
      }
    }

    return v;
  }
}

bool PtrResidueRemediator::may_alias(
    const Value *P1, unsigned size1, const Value *P2, unsigned size2,
    const Loop *L, SpecPriv::PtrResidueSpeculationManager::Assumption &ass1_out,
    SpecPriv::PtrResidueSpeculationManager::Assumption &ass2_out) const {
  if (!L)
    return true;

  if (!P1->getType()->isPointerTy())
    return true;
  if (!P2->getType()->isPointerTy())
    return true;

  if (P1 == P2)
    return true;

  ++numApplicable;

  int correction1 = 0;
  const Value *ptr1 = adjust_pointer(P1, *td, correction1);

  int correction2 = 0;
  const Value *ptr2 = adjust_pointer(P2, *td, correction2);

  const Read &read = manager->getSpecPrivResult();
  const Ctx *ctx = read.getCtx(L);

  DEBUG(errs() << "ptr-residue-aa(" << *P1 << ", " << *P2 << ")\n");

  const uint16_t residual1 = read.getPointerResiduals(ptr1, ctx);
  DEBUG(errs() << " residue(" << *ptr1 << ") = " << residual1 << '\n');
  if (residual1 == 0 || residual1 == 0x0ffffu)
    return true;

  const uint16_t residual2 = read.getPointerResiduals(ptr2, ctx);
  DEBUG(errs() << " residue(" << *ptr2 << ") = " << residual2 << '\n');
  if (residual2 == 0 || residual2 == 0x0ffffu)
    return true;

  DEBUG(errs() << " Pointer ``" << *ptr1 << "'' has residuals " << residual1
               << ", correction " << correction1 << '\n';
        errs() << " Pointer ``" << *ptr2 << "'' has residuals " << residual2
               << ", correction " << correction2 << '\n';);

  if (residues_overlap(residual1, size1, correction1, residual2, size2,
                       correction2))
    return true;

  ass1_out = SpecPriv::PtrResidueSpeculationManager::Assumption(ptr1, ctx);
  ass2_out = SpecPriv::PtrResidueSpeculationManager::Assumption(ptr2, ctx);

  DEBUG(errs() << " --> NoAlias\n");
  return false;
}

bool PtrResidueRemediator::may_modref(
    const Instruction *A, const Value *P2, unsigned S2, const Loop *L,
    SpecPriv::PtrResidueSpeculationManager::Assumption &ass_out1,
    SpecPriv::PtrResidueSpeculationManager::Assumption &ass_out2) const {
  if (const StoreInst *store = dyn_cast<StoreInst>(A)) {
    const Value *P1 = store->getPointerOperand();
    unsigned S1 = td->getTypeStoreSize(store->getValueOperand()->getType());

    if (!may_alias(P1, S1, P2, S2, L, ass_out1, ass_out2))
      return false;
  } else if (const LoadInst *load = dyn_cast<LoadInst>(A)) {
    const Value *P1 = load->getPointerOperand();
    unsigned S1 = td->getTypeStoreSize(load->getType());

    if (!may_alias(P1, S1, P2, S2, L, ass_out1, ass_out2))
      return false;
  }

  return true;
}

Remediator::RemedResp PtrResidueRemediator::memdep(const Instruction *A,
                                                   const Instruction *B,
                                                   bool LoopCarried,
                                                   DataDepType dataDepTy,
                                                   const Loop *L) {

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<PtrResidueRemedy> remedy =
      std::shared_ptr<PtrResidueRemedy>(new PtrResidueRemedy());
  //remedy->cost = DEFAULT_PTR_RESIDUE_REMED_COST;

  if (!td)
    td = &A->getModule()->getDataLayout();

  SpecPriv::PtrResidueSpeculationManager::Assumption a1, a2;
  const Value *P2 = 0;
  unsigned S2 = 0;
  if (const StoreInst *store = dyn_cast<StoreInst>(B)) {
    P2 = store->getPointerOperand();
    S2 = td->getTypeStoreSize(store->getValueOperand()->getType());
  }

  else if (const LoadInst *load = dyn_cast<LoadInst>(B)) {
    P2 = load->getPointerOperand();
    S2 = td->getTypeStoreSize(load->getType());
  }

  if (P2) {
    if (!may_modref(A, P2, S2, L, a1, a2)) {
      // We can report no-mod-ref.

      // Speculation is required to report no-mod-ref.
      // Record this requirement.
      manager->setAssumed(a1);
      manager->setAssumed(a2);
      ++numNoMemDep;
      remedResp.depRes = DepResult::NoDep;
      remedy->setCost(perf, a1.first);
      //remedy->setCost(perf, a1.first, a2.first);
      remedy->ptr1 = a1.first;
      remedy->ctx1 = a1.second;
      remedy->ptr2 = a2.first;
      remedy->ctx2 = a2.second;
    }
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
