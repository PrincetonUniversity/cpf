#define DEBUG_TYPE "asap"

#include "llvm/ADT/Statistic.h"
#include "scaf/Utilities/FindUnderlyingObjects.h"

#include "liberty/Orchestration/TXIOAA.h"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/GetMemOper.h"

#ifndef DEFAULT_TXIO_REMED_COST
#define DEFAULT_TXIO_REMED_COST 20
#endif

namespace liberty
{
using namespace llvm;
using namespace llvm::noelle;

STATISTIC(numTXIO, "Number of NoModRef from txio");

bool TXIORemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<TXIORemedy> txioRhs =
      std::static_pointer_cast<TXIORemedy>(rhs);
  return this->printI < txioRhs->printI;
}

bool TXIOAA::isTXIOFcn(const Instruction *inst) {
  CallSite cs = getCallSite(inst);
  if (!cs.getInstruction())
    return false;

  Function *callee = cs.getCalledFunction();
  if (!callee)
    return false;

  if (callee->getName() == "vfprintf")
    return true;
  else if (callee->getName() == "vprintf")
    return true;
  else if (callee->getName() == "fprintf")
    return true;
  else if (callee->getName() == "printf")
    return true;
  else if (callee->getName() == "fputs")
    return true;
  else if (callee->getName() == "puts")
    return true;
  else if (callee->getName() == "fputc")
    return true;
  else if (callee->getName() == "putc")
    return true;
  else if (callee->getName() == "putchar")
    return true;
  else if (callee->getName() == "fflush")
    return true;
  // temporarily for dijistra, print_path is
  else if (callee->getName() == "print_path")
    return true;

  return false;
}

LoopAA::AliasResult TXIOAA::alias(const Value *ptrA, unsigned sizeA,
                                  TemporalRelation rel, const Value *ptrB,
                                  unsigned sizeB, const Loop *L, Remedies &R,
                                  DesiredAliasResult dAliasRes) {
  return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);
}

LoopAA::ModRefResult TXIOAA::modref(const Instruction *A, TemporalRelation rel,
                                    const Value *ptrB, unsigned sizeB,
                                    const Loop *L, Remedies &R) {
  if (rel == LoopAA::Same)
    return LoopAA::modref(A, rel, ptrB, sizeB, L, R);

  std::shared_ptr<TXIORemedy> remedy =
      std::shared_ptr<TXIORemedy>(new TXIORemedy());
  remedy->cost = DEFAULT_TXIO_REMED_COST;

  if (isTXIOFcn(A)) {
    ++numTXIO;
    remedy->printI = A;
    R.insert(remedy);
    //return Ref;
    return NoModRef;
  }

  return LoopAA::modref(A, rel, ptrB, sizeB, L, R);
}

LoopAA::ModRefResult TXIOAA::modref(const Instruction *A, TemporalRelation rel,
                                    const Instruction *B, const Loop *L,
                                    Remedies &R) {
  if (rel == LoopAA::Same)
    return LoopAA::modref(A, rel, B, L, R);

  std::shared_ptr<TXIORemedy> remedy =
      std::shared_ptr<TXIORemedy>(new TXIORemedy());
  remedy->cost = DEFAULT_TXIO_REMED_COST;

  if (isTXIOFcn(A)) {
    ++numTXIO;
    remedy->printI = A;
    R.insert(remedy);
    //return Ref;
    return NoModRef;
  }

  if (isTXIOFcn(B)) {
    ++numTXIO;
    remedy->printI = B;
    R.insert(remedy);
    //return Ref;
    return NoModRef;
  }

  return LoopAA::modref(A, rel, B, L, R);
}
}

