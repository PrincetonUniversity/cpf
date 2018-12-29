#define DEBUG_TYPE "prediction-aa"

#include "llvm/ADT/Statistic.h"
#include "liberty/Analysis/PredictionSpeculation.h"

namespace liberty
{

STATISTIC(numNoAlias, "Num no-alias / no-modref");

bool NoPredictionSpeculation::isPredictable(const Instruction *I, const Loop *loop)
{
  return false;
}

LoopAA::ModRefResult PredictionAA::modref(
    const Instruction *I1,
    TemporalRelation rel,
    const Value *P2,
    unsigned S2,
    const Loop *L)
{
  return LoopAA::modref(I1,rel,P2,S2,L);
}

LoopAA::ModRefResult PredictionAA::modref(
    const Instruction *I1,
    TemporalRelation rel,
    const Instruction *I2,
    const Loop *L)
{
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

  return LoopAA::modref(I1,rel,I2,L);
}

}

