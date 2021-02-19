#define DEBUG_TYPE "prediction-aa"

#include "llvm/ADT/Statistic.h"
#include "liberty/Orchestration/PredictionSpeculation.h"
#include "scaf/Utilities/GetMemOper.h"
#include "llvm/Analysis/ValueTracking.h"

#include "scaf/Utilities/FindUnderlyingObjects.h"

#ifndef DEFAULT_LOADED_VALUE_PRED_REMED_COST
#define DEFAULT_LOADED_VALUE_PRED_REMED_COST 58
#endif

namespace liberty
{

STATISTIC(numNoAlias, "Num no-alias / no-modref");
STATISTIC(numSubQueries,  "Num sub-queries spawned");

bool NoPredictionSpeculation::isPredictable(const Instruction *I, const Loop *loop)
{
  return false;
}

bool LoadedValuePredRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<LoadedValuePredRemedy> valPredRhs =
      std::static_pointer_cast<LoadedValuePredRemedy>(rhs);
  //if (this->ptr == valPredRhs->ptr)
  //  return this->write < valPredRhs->write;
  return this->ptr < valPredRhs->ptr;
}

void LoadedValuePredRemedy::setCost(PerformanceEstimator *perf,
                                             const Loop *loop) {
  // 1 cmp, 1 branch
  unsigned validation_weight = 101;
  const Instruction *gravity = loop->getHeader()->getTerminator();
  assert(gravity && "no terminator in BB??");
  this->cost =
      Remediator::estimate_validation_weight(perf, gravity, validation_weight);
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
          mapPtrsToLoad[ptr] = load;
        }
      }
    }
  }
  DL = &loop->getHeader()->getModule()->getDataLayout();
  this->L = loop;
}

/// No-topping case of pointer comparison.
bool PredictionAA::mustAliasFast(const Value *ptr1, const Value *ptr2) {
  assert(DL && "forgot to call PredictionAA::setLoopOfInterest??");
  UO a, b;
  GetUnderlyingObjects(ptr1, a, *DL);
  if (a.size() != 1)
    return false;
  GetUnderlyingObjects(ptr2, b, *DL);
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
  return top->alias(ptr1, 1, LoopAA::Same, ptr2, 1, 0, R, LoopAA::DMustAlias) ==
         MustAlias;
}

bool PredictionAA::isPredictablePtr(const Value *ptr) {
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
      if (mustAliasFast(predPtr, ptr) || mustAlias(predPtr, ptr)) {
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

LoopAA::AliasResult PredictionAA::alias(const Value *ptrA, unsigned sizeA,
                                        TemporalRelation rel, const Value *ptrB,
                                        unsigned sizeB, const Loop *L,
                                        Remedies &R,
                                        DesiredAliasResult dAliasRes) {

  if (dAliasRes == DMustAlias)
    return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);

  if (rel == LoopAA::Same)
    return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R);

  if (!ptrA || !ptrB)
    return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R);

  Remedies tmpR;
  AliasResult result = MayAlias;

  std::shared_ptr<LoadedValuePredRemedy> remedy =
      std::shared_ptr<LoadedValuePredRemedy>(new LoadedValuePredRemedy());
  //remedy->cost = DEFAULT_LOADED_VALUE_PRED_REMED_COST;

  bool predPtrA = isPredictablePtr(ptrA);
  bool predPtrB = isPredictablePtr(ptrB);

  if (predPtrA || predPtrB) {
    ++numNoAlias;
    if (predPtrA) {
      remedy->ptr = mustAliasWithPredictableMemLocMap[ptrA];
      //if (I1->mayWriteToMemory())
      //  remedy->write = true;
    } else {
      remedy->ptr = mustAliasWithPredictableMemLocMap[ptrB];
      // if (I2->mayWriteToMemory())
      //  remedy->write = true;
    }
    remedy->loadI = mapPtrsToLoad[remedy->ptr];
    remedy->setCost(perf, this->L);
    tmpR.insert(remedy);
    result = NoAlias;
    return LoopAA::chain(R, ptrA, sizeA, rel, ptrB, sizeB, L, result, tmpR);
  }

  return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R);
}

LoopAA::ModRefResult PredictionAA::modref(const Instruction *I1,
                                          TemporalRelation rel, const Value *P2,
                                          unsigned S2, const Loop *L,
                                          Remedies &R) {

  std::shared_ptr<LoadedValuePredRemedy> remedy =
      std::shared_ptr<LoadedValuePredRemedy>(new LoadedValuePredRemedy());
  //remedy->cost = DEFAULT_LOADED_VALUE_PRED_REMED_COST;

  Remedies tmpR;
  ModRefResult result = ModRef;

  const Value *ptrA = liberty::getMemOper(I1);
  if (predspec->isPredictable(I1, L)) {
    ++numNoAlias;
    remedy->ptr = ptrA;
    remedy->loadI = I1;
    remedy->setCost(perf, this->L);
    tmpR.insert(remedy);
    result = NoModRef;
    return LoopAA::chain(R, I1, rel, P2, S2, L, result, tmpR);
  }

  if (rel == LoopAA::Same)
    return LoopAA::modref(I1, rel, P2, S2, L, R);

  if (!ptrA)
    return LoopAA::modref(I1, rel, P2, S2, L, R);

  bool predPtrA = isPredictablePtr(ptrA);
  bool predPtrB = isPredictablePtr(P2);

  if (predPtrA || predPtrB) {
    ++numNoAlias;
    if (predPtrA) {
      remedy->ptr = mustAliasWithPredictableMemLocMap[ptrA];
      if (I1->mayWriteToMemory())
        remedy->write = true;
    } else {
      remedy->ptr = mustAliasWithPredictableMemLocMap[P2];
      //if (I2->mayWriteToMemory())
      //  remedy->write = true;
    }
    remedy->loadI = mapPtrsToLoad[remedy->ptr];
    remedy->setCost(perf, this->L);
    tmpR.insert(remedy);
    result = NoModRef;
    return LoopAA::chain(R, I1, rel, P2, S2, L, result, tmpR);
  }

  return LoopAA::modref(I1,rel,P2,S2,L,R);
}

LoopAA::ModRefResult PredictionAA::modref(const Instruction *I1,
                                          TemporalRelation rel,
                                          const Instruction *I2, const Loop *L,
                                          Remedies &R) {

  std::shared_ptr<LoadedValuePredRemedy> remedy =
      std::shared_ptr<LoadedValuePredRemedy>(new LoadedValuePredRemedy());
  //remedy->cost = DEFAULT_LOADED_VALUE_PRED_REMED_COST;

  Remedies tmpR;
  ModRefResult result = ModRef;

  const Value *ptrA = liberty::getMemOper(I1);
  const Value *ptrB = liberty::getMemOper(I2);

  bool predA = predspec->isPredictable(I1, L);
  bool predB = predspec->isPredictable(I2, L);
  if (predA || predB) {
    ++numNoAlias;
    remedy->ptr = (predA) ? ptrA : ptrB;
    remedy->loadI = (predA) ? I1 : I2;
    remedy->setCost(perf, this->L);
    tmpR.insert(remedy);
    result = NoModRef;
    return LoopAA::chain(R, I1, rel, I2, L, result, tmpR);
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

  if (!ptrA || !ptrB)
    return LoopAA::modref(I1, rel, I2, L, R);

  bool predPtrA = isPredictablePtr(ptrA);
  bool predPtrB = isPredictablePtr(ptrB);

  if (predPtrA || predPtrB) {
    ++numNoAlias;
    if (predPtrA) {
      remedy->ptr = mustAliasWithPredictableMemLocMap[ptrA];
      if (I1->mayWriteToMemory())
        remedy->write = true;
    } else {
      remedy->ptr = mustAliasWithPredictableMemLocMap[ptrB];
      if (I2->mayWriteToMemory())
        remedy->write = true;
    }
    remedy->loadI = mapPtrsToLoad[remedy->ptr];
    remedy->setCost(perf, this->L);
    tmpR.insert(remedy);
    result = NoModRef;
    return LoopAA::chain(R, I1, rel, I2, L, result, tmpR);
  }

  return LoopAA::modref(I1, rel, I2, L, R);
}

}

