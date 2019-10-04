#define DEBUG_TYPE "prediction-aa"

#include "llvm/ADT/Statistic.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/Utilities/GetMemOper.h"
#include "llvm/Analysis/ValueTracking.h"


#include "liberty/Utilities/FindUnderlyingObjects.h"

namespace liberty
{

STATISTIC(numNoAlias, "Num no-alias / no-modref");
STATISTIC(numSubQueries,  "Num sub-queries spawned");

bool NoPredictionSpeculation::isPredictable(const Instruction *I, const Loop *loop)
{
  return false;
}

void PredictionAA::setLoopOfInterest(Loop *loop) {
  predictableMemLocs.clear();
  nonPredictableMemLocs.clear();
  mustAliasWithPredictableMemLocMap.clear();

  for (Loop::block_iterator i = loop->block_begin(), e = loop->block_end();
       i != e; ++i) {
    BasicBlock *bb = *i;
    for (BasicBlock::iterator j = bb->begin(), z = bb->end(); j != z; ++j) {
      Instruction *I = &*j;
      if (LoadInst *load = dyn_cast<LoadInst>(I)) {
        if (predspec->isPredictable(load, loop)) {
          Value *ptr = load->getPointerOperand();
          predictableMemLocs.insert(ptr);
        }
      }
    }
  }
}

/// No-topping case of pointer comparison.
bool PredictionAA::mustAliasFast(const Value *ptr1, const Value *ptr2,
                                 const DataLayout &DL) {
  UO a, b;
  GetUnderlyingObjects(ptr1, a, DL);
  if (a.size() != 1)
    return false;
  GetUnderlyingObjects(ptr2, b, DL);
  return a == b;
}

bool PredictionAA::mustAlias(const Value *ptr1, const Value *ptr2) {
  // Very easy case
  if (ptr1 == ptr2 && isa<GlobalValue>(ptr1))
    return true;

  LoopAA *top = getTopAA();
  ++numSubQueries;

  // no remedies needed for mustAlias queries. No spec returns must alias for
  // now
  Remedies R;
  return top->alias(ptr1, 1, LoopAA::Same, ptr2, 1, 0, R) == MustAlias;
}

bool PredictionAA::isPredictablePtr(const Value *ptr, const DataLayout &DL) {
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

LoopAA::ModRefResult PredictionAA::modref(const Instruction *I1,
                                          TemporalRelation rel, const Value *P2,
                                          unsigned S2, const Loop *L,
                                          Remedies &R) {
  if (predspec->isPredictable(I1, L)) {
    ++numNoAlias;
    return NoModRef;
  }

  if (rel == LoopAA::Same)
    return LoopAA::modref(I1, rel, P2, S2, L, R);

  const Value *ptrA = liberty::getMemOper(I1);
  if (!ptrA)
    return LoopAA::modref(I1, rel, P2, S2, L, R);

  const Module *M = I1->getModule();
  const DataLayout &DL = M->getDataLayout();

  bool predPtrA = isPredictablePtr(ptrA, DL);
  bool predPtrB = isPredictablePtr(P2, DL);

  if (predPtrA || predPtrB) {
    ++numNoAlias;
    return NoModRef;
  }

  return LoopAA::modref(I1,rel,P2,S2,L,R);
}

LoopAA::ModRefResult PredictionAA::modref(const Instruction *I1,
                                          TemporalRelation rel,
                                          const Instruction *I2, const Loop *L,
                                          Remedies &R) {
  if (predspec->isPredictable(I2, L) || predspec->isPredictable(I1, L)) {
    ++numNoAlias;
    return NoModRef;
  }

  /*
  if( rel == LoopAA::Before && predspec->isPredictable(I2,L) )
  {
    ++numNoAlias;
    return NoModRef;
  }
  else if( rel == LoopAA::After && predspec->isPredictable(I1,L) )
  {
    ++numNoAlias;
    return NoModRef;
  }
  */

  if (rel == LoopAA::Same)
    return LoopAA::modref(I1, rel, I2, L, R);

  const Value *ptrA = liberty::getMemOper(I1);
  const Value *ptrB = liberty::getMemOper(I2);

  if (!ptrA || !ptrB)
    return LoopAA::modref(I1, rel, I2, L, R);

  const Module *M = I1->getModule();
  const DataLayout &DL = M->getDataLayout();

  bool predPtrA = isPredictablePtr(ptrA, DL);
  bool predPtrB = isPredictablePtr(ptrB, DL);

  if (predPtrA || predPtrB) {
    ++numNoAlias;
    return NoModRef;
  }

  return LoopAA::modref(I1, rel, I2, L, R);
}

}

