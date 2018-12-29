#define DEBUG_TYPE "asap"

#include "llvm/ADT/Statistic.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"

#include "liberty/Analysis/TXIOAA.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/CallSiteFactory.h"


namespace liberty
{
using namespace llvm;

STATISTIC(numTXIO, "Number of NoModRef from txio");

bool TXIOAA::isTXIOFcn(const Instruction *inst) {
  CallSite cs = getCallSite(inst);
  if( !cs.getInstruction() ) return false;

  Function *callee = cs.getCalledFunction();
  if( !callee ) return false;

  if( callee->getName() == "vfprintf" ) return true;
  else if( callee->getName() == "vprintf" ) return true;
  else if( callee->getName() == "fprintf" ) return true;
  else if( callee->getName() == "printf" ) return true;
  else if( callee->getName() == "fputs" ) return true;
  else if( callee->getName() == "puts" ) return true;
  else if( callee->getName() == "fputc" ) return true;
  else if( callee->getName() == "putc" ) return true;
  else if( callee->getName() == "putchar" ) return true;

  return false;
}

LoopAA::AliasResult TXIOAA::alias(
    const Value *ptrA, unsigned sizeA,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
{
  return LoopAA::alias(ptrA,sizeA,rel,ptrB,sizeB,L);
}


LoopAA::ModRefResult TXIOAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L) {
  if(rel == LoopAA::Same)
    return LoopAA::modref(A,rel,ptrB,sizeB,L);

  if(isTXIOFcn(A)) {
    ++numTXIO;
    return Ref;
  }

  return LoopAA::modref(A,rel,ptrB,sizeB,L);
}

LoopAA::ModRefResult TXIOAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L) {
  if(rel == LoopAA::Same)
    return LoopAA::modref(A,rel,B,L);

  if(isTXIOFcn(A)) {
    ++numTXIO;
    return Ref;
  }

  return LoopAA::modref(A,rel,B,L);
}


}

