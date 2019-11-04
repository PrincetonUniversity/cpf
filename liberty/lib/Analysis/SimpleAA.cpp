#include "liberty/Analysis/SimpleAA.h"

namespace liberty
{
using namespace llvm;

LoopAA::AliasResult SimpleAA::alias(const Value *ptrA, unsigned sizeA,
                                    TemporalRelation rel, const Value *ptrB,
                                    unsigned sizeB, const Loop *L, Remedies &R,
                                    DesiredAliasResult dAliasRes) {
  return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R);
}

LoopAA::ModRefResult SimpleAA::modref(const Instruction *A,
                                      TemporalRelation rel, const Value *ptrB,
                                      unsigned sizeB, const Loop *L,
                                      Remedies &R) {

  if (!A->mayReadOrWriteMemory())
    return NoModRef;

  Remedies tmpR;
  ModRefResult result = LoopAA::modref(A, rel, ptrB, sizeB, L, tmpR);

  if (!A->mayReadFromMemory()) {
    if (result == NoModRef || result == Ref) {
      for (auto remed : tmpR) {
        R.insert(remed);
      }
    }
    result = ModRefResult(result & ~Ref);
  } else if (!A->mayWriteToMemory()) {
    if (result == NoModRef || result == Mod) {
      for (auto remed : tmpR) {
        R.insert(remed);
      }
    }
    result = ModRefResult(result & ~Mod);
  } else {
    for (auto remed : tmpR) {
      R.insert(remed);
    }
  }

  return result;
}

LoopAA::ModRefResult SimpleAA::modref(const Instruction *A, TemporalRelation rel,
                                    const Instruction *B, const Loop *L,
                                    Remedies &R) {

  if (!A->mayReadOrWriteMemory() || !B->mayReadOrWriteMemory())
    return NoModRef;

  Remedies tmpR;
  ModRefResult result = LoopAA::modref(A, rel, B, L, tmpR);

  if (!A->mayReadFromMemory()) {
    if (result == NoModRef || result == Ref) {
      for (auto remed : tmpR) {
        R.insert(remed);
      }
    }
    result = ModRefResult(result & ~Ref);
  } else if (!A->mayWriteToMemory()) {
    if (result == NoModRef || result == Mod) {
      for (auto remed : tmpR) {
        R.insert(remed);
      }
    }
    result = ModRefResult(result & ~Mod);
  } else {
    for (auto remed : tmpR) {
      R.insert(remed);
    }
  }

  return result;

}
}
