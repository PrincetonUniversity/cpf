#define DEBUG_TYPE "ctrlspec"

#include "liberty/Analysis/Introspection.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ControlSpecIterators.h"
#include "liberty/Orchestration/EdgeCountOracleAA.h"
#include "liberty/Utilities/Timer.h"
#include "liberty/Orchestration/ControlSpecRemed.h"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#ifndef DEFAULT_CTRL_REMED_COST
#define DEFAULT_CTRL_REMED_COST 45
#endif

namespace liberty
{
using namespace llvm;

STATISTIC(numQueries,          "Num queries in cntr spec AA");
STATISTIC(numNoModRef,         "Num no-mod-ref results in cntr spec AA");

LoopAA::ModRefResult EdgeCountOracle::modref(
  const Instruction *A,
  TemporalRelation rel,
  const Value *ptrB, unsigned sizeB,
  const Loop *L, Remedies &R)
{
  ++numQueries;

  INTROSPECT(ENTER(A,rel,ptrB,sizeB,L));

  std::shared_ptr<ControlSpecRemedy> remedy =
      std::shared_ptr<ControlSpecRemedy>(new ControlSpecRemedy());
  remedy->cost = DEFAULT_CTRL_REMED_COST;
  remedy->brI = nullptr;

  if( speculator->isSpeculativelyDead( A ) )
  {
    ++numNoModRef;
    R.insert(remedy);
    return NoModRef;
  }

  INTROSPECT(EXIT(A,rel,ptrB,sizeB,L));
  // Chain.
  return LoopAA::modref(A,rel,ptrB,sizeB,L,R);
}


LoopAA::ModRefResult EdgeCountOracle::modref(
  const Instruction *A,
  TemporalRelation rel,
  const Instruction *B,
  const Loop *L, Remedies &R)
{
  ++numQueries;

  INTROSPECT(ENTER(A,rel,B,L));

  std::shared_ptr<ControlSpecRemedy> remedy =
      std::shared_ptr<ControlSpecRemedy>(new ControlSpecRemedy());
  remedy->cost = DEFAULT_CTRL_REMED_COST;
  remedy->brI = nullptr;

  if( speculator->isSpeculativelyDead( A ) )
  {
    ++numNoModRef;
    INTROSPECT(EXIT(A,rel,B,L,NoModRef));
    R.insert(remedy);
    return NoModRef;
  }

  if( speculator->isSpeculativelyDead( B ) )
  {
    ++numNoModRef;
    INTROSPECT(EXIT(A,rel,B,L,NoModRef));
    R.insert(remedy);
    return NoModRef;
  }

  INTROSPECT(EXIT(A,rel,B,L));
  // Chain.
  return LoopAA::modref(A,rel,B,L,R);
}

}

