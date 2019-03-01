#define DEBUG_TYPE "locality-remed"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallBitVector.h"

#include "liberty/Orchestration/LocalityRemed.h"

#define DEFAULT_LOCALITY_REMED_COST 40
#define PRIVATE_ACCESS_COST 100
#define LOCAL_ACCESS_COST 5

namespace liberty {
using namespace llvm;

STATISTIC(numEligible,   "Num eligible queries");
STATISTIC(numPrivatized, "Num privatized");
STATISTIC(numSeparated,  "Num separated");
STATISTIC(numSubSep,     "Num separated via subheaps");

void LocalityRemedy::apply(PDG &pdg) {
  // TODO: transfer the code for application of separation logic here.
}

bool LocalityRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<LocalityRemedy> sepRhs =
      std::static_pointer_cast<LocalityRemedy>(rhs);

  if (this->privateI == sepRhs->privateI)
    return this->localI < sepRhs->localI;
  return this->privateI < sepRhs->privateI;
}

Remediator::RemedResp LocalityRemediator::memdep(const Instruction *A,
                                                 const Instruction *B,
                                                 const bool LoopCarried,
                                                 const Loop *L) {

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<LocalityRemedy> remedy =
      std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
  remedy->cost = DEFAULT_LOCALITY_REMED_COST;
  remedResp.remedy = remedy;

  if (!L || !asgn.isValidFor(L))
    return remedResp;

  const Value *ptr1 = liberty::getMemOper(A);
  const Value *ptr2 = liberty::getMemOper(B);
  if (!ptr1 || !ptr2)
    return remedResp;
  if (!isa<PointerType>(ptr1->getType()))
    return remedResp;
  if (!isa<PointerType>(ptr2->getType()))
    return remedResp;

  const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptr1, ctx, aus1))
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptr2, ctx, aus2))
    t2 = asgn.classify(aus2);

  // Loop-carried queries:
  if (LoopCarried) {
    // Reduction, local and private heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if (t1 == HeapAssignment::Redux || t1 == HeapAssignment::Local ||
        t1 == HeapAssignment::Private) {
      ++numPrivatized;
      remedResp.depRes = DepResult::NoDep;
      if (t1 == HeapAssignment::Private) {
        remedy->cost += PRIVATE_ACCESS_COST;
        remedy->privateI = A;
        remedy->localI = nullptr;
      } else if (t1 == HeapAssignment::Local) {
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->privateI = nullptr;
        remedy->localI = A;
      } else {
        remedy->privateI = nullptr;
        remedy->localI = nullptr;
      }
      remedResp.remedy = remedy;
      return remedResp;
    }

    if (t2 == HeapAssignment::Redux || t2 == HeapAssignment::Local ||
        t2 == HeapAssignment::Private) {
      ++numPrivatized;
      remedResp.depRes = DepResult::NoDep;
      if (t2 == HeapAssignment::Private) {
        remedy->cost += PRIVATE_ACCESS_COST;
        remedy->privateI = B;
        remedy->localI = nullptr;
      } else if (t2 == HeapAssignment::Local) {
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->privateI = nullptr;
        remedy->localI = B;
      } else {
        remedy->privateI = nullptr;
        remedy->localI = nullptr;
      }
      remedResp.remedy = remedy;
      return remedResp;
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to
  // different heaps?
  if (t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    remedResp.depRes = DepResult::NoDep;
    remedy->privateI = nullptr;
    remedy->localI = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  // They are assigned to the same heap.
  // Are they assigned to different sub-heaps?
  if (t1 == t2 && t1 != HeapAssignment::Unclassified) {
    const int subheap1 = asgn.getSubHeap(aus1);
    if (subheap1 > 0) {
      const int subheap2 = asgn.getSubHeap(aus2);
      if (subheap2 > 0 && subheap1 != subheap2) {
        ++numSubSep;
        remedResp.depRes = DepResult::NoDep;
        remedy->privateI = nullptr;
        remedy->localI = nullptr;
        remedResp.remedy = remedy;
        return remedResp;
      }
    }
  }

  return remedResp;
}

} // namespace liberty
