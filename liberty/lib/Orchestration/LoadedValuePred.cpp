#define DEBUG_TYPE "loaded-value-pred-remed"

#include "liberty/Utilities/GetMemOper.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"

#include "liberty/Orchestration/LoadedValuePredRemed.h"

#define DEFAULT_LOADED_VALUE_PRED_REMED_COST 58

namespace liberty {
using namespace llvm;

STATISTIC(numNoMemDep, "Number of mem deps removed by loaded-value-pred-remed");

void LoadedValuePredRemedy::apply(Task *task) {
  // TODO: code for application of loaded-value-pred-remed here.
}

bool LoadedValuePredRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<LoadedValuePredRemedy> valPredRhs =
      std::static_pointer_cast<LoadedValuePredRemedy>(rhs);
  if (this->ptr == valPredRhs->ptr)
    return this->write < valPredRhs->write;
  return this->ptr < valPredRhs->ptr;
}

const Value *LoadedValuePredRemediator::getPtr(const Instruction *I,
                                               DataDepType dataDepTy) {
  const Value *ptr = liberty::getMemOper(I);
  // if ptr null, check for memcpy/memmove inst.
  // src pointer is read, dst pointer is written.
  // choose pointer for current query based on dataDepTy
  if (!ptr) {
    if (const MemTransferInst *mti = dyn_cast<MemTransferInst>(I)) {
      if (dataDepTy == DataDepType::WAR)
        ptr = mti->getRawSource();
      ptr = mti->getRawDest();
    }
  }
  return ptr;
}

Remedies LoadedValuePredRemediator::satisfy(const PDG &pdg, Loop *loop,
                                            const Criticisms &criticisms) {
  for (auto nodeI : make_range(pdg.begin_nodes(), pdg.end_nodes())) {
    Value *pdgValueI = nodeI->getT();
    LoadInst *load = dyn_cast<LoadInst>(pdgValueI);
    if (!load)
      continue;

    if (predspec->isPredictable(load, loop)) {
      Value *ptr = load->getPointerOperand();
      predictableMemLocs.insert(ptr);
    }
  }

  Remedies remedies = Remediator::satisfy(pdg, loop, criticisms);

  // print number
  DEBUG(errs() << "Number of RAW collab deps handled by LoadedValuePredRemed: " << RAWcollabDepsHandled << '\n');
  DEBUG(errs() << "Number of WAW collab deps handled by LoadedValuePredRemed: " << WAWcollabDepsHandled << '\n');

  return remedies;
}

/// No-loopAA case of pointer comparison.
bool LoadedValuePredRemediator::mustAliasFast(const Value *ptr1,
                                              const Value *ptr2,
                                              const DataLayout &DL) {
  UO a, b;
  GetUnderlyingObjects(ptr1, a, DL);
  if (a.size() != 1)
    return false;
  GetUnderlyingObjects(ptr2, b, DL);
  return a == b;
}

bool LoadedValuePredRemediator::mustAlias(const Value *ptr1,
                                          const Value *ptr2) {
  // Very easy case
  if (ptr1 == ptr2 && isa<GlobalValue>(ptr1))
    return true;

  return loopAA->alias(ptr1, 1, LoopAA::Same, ptr2, 1, 0) == MustAlias;
}

bool LoadedValuePredRemediator::isPredictablePtr(const Value *ptr,
                                                 const DataLayout &DL) {
  if (!ptr || nonPredictableMemLocs.count(ptr))
    return false;

  bool isPredPtr = predictableMemLocs.count(ptr);
  if (isPredPtr)
    mustAliasWithPredictableMemLocMap[ptr] = ptr;
  else
    isPredPtr = mustAliasWithPredictableMemLocMap.count(ptr);

  if (!isPredPtr) {
    // check if ptr must alias with any of the predictable pointers
    for (auto predPtr : predictableMemLocs) {
      if (mustAliasFast(predPtr, ptr, DL) || mustAlias(predPtr, ptr)) {
        mustAliasWithPredictableMemLocMap[ptr] = predPtr;
        isPredPtr = true;
        break;
      }
    }
  }

  if (!isPredPtr)
    nonPredictableMemLocs.insert(ptr);
  return isPredPtr;
}

Remediator::RemedResp LoadedValuePredRemediator::memdep(const Instruction *A,
                                                        const Instruction *B,
                                                        bool loopCarried,
                                                        DataDepType dataDepTy,
                                                        const Loop *L) {
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  std::shared_ptr<LoadedValuePredRemedy> remedy =
      std::shared_ptr<LoadedValuePredRemedy>(new LoadedValuePredRemedy());
  remedy->cost = DEFAULT_LOADED_VALUE_PRED_REMED_COST;
  const DataLayout &DL = A->getModule()->getDataLayout();

  // if A or B is a loop-invariant load instruction report no dep
  const Value *ptrA = getPtr(A, dataDepTy);
  const Value *ptrB = getPtr(B, dataDepTy);
  bool predA = predspec->isPredictable(A, L);
  bool predB = predspec->isPredictable(B, L);
  bool predictableI = false;
  if (predA || predB) {
    ++numNoMemDep;
    remedy->ptr = (predA) ? ptrA : ptrB;
    remedResp.depRes = DepResult::NoDep;

    predictableI = true;

    DEBUG(errs() << "LoadedValuePredRemed removed mem dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  bool predPtrA = isPredictablePtr(ptrA, DL);
  bool predPtrB = isPredictablePtr(ptrB, DL);
  if (predPtrA || predPtrB) {
    ++numNoMemDep;
    if (predPtrA) {
      remedy->ptr = mustAliasWithPredictableMemLocMap[ptrA];
      if (A->mayWriteToMemory())
        remedy->write = true;
    } else {
      remedy->ptr = mustAliasWithPredictableMemLocMap[ptrB];
      if (B->mayWriteToMemory())
        remedy->write = true;
    }
    remedResp.depRes = DepResult::NoDep;

    if ( loopCarried && dataDepTy == DataDepType::WAW && !predictableI)
      WAWcollabDepsHandled++;
    if ( loopCarried && dataDepTy == DataDepType::RAW && !predictableI)
      RAWcollabDepsHandled++;

    DEBUG(errs() << "LoadedValuePredRemed removed mem dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
