#define DEBUG_TYPE "ctrlspec"

#include "liberty/Analysis/Introspection.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ControlSpecIterators.h"
#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/Utilities/Timer.h"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"



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

  ModRefResult result = ModRef;

  if( speculator->isSpeculativelyDead( A ) )
  {
    ++numNoModRef;
    result = NoModRef;
  }

  if( result != NoModRef )
    // Chain.
    result = ModRefResult(result & LoopAA::modref(A,rel,ptrB,sizeB,L,R) );

  INTROSPECT(EXIT(A,rel,ptrB,sizeB,L));
  return result;
}


LoopAA::ModRefResult EdgeCountOracle::modref(
  const Instruction *A,
  TemporalRelation rel,
  const Instruction *B,
  const Loop *L, Remedies &R)
{
  ++numQueries;

  INTROSPECT(ENTER(A,rel,B,L));

  ModRefResult result = ModRef;

  if( speculator->isSpeculativelyDead( A ) )
  {
    ++numNoModRef;
    INTROSPECT(EXIT(A,rel,B,L,NoModRef));
    return NoModRef;
  }

  if( speculator->isSpeculativelyDead( B ) )
  {
    ++numNoModRef;
    INTROSPECT(EXIT(A,rel,B,L,NoModRef));
    return NoModRef;
  }

  // Chain.
  result = ModRefResult(result & LoopAA::modref(A,rel,B,L,R) );

  INTROSPECT(EXIT(A,rel,B,L,result));
  return result;
}

}

