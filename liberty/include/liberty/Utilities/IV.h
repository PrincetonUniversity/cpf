#ifndef IV_UTIL_H
#define IV_UTIL_H

#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace liberty {

  PHINode *getInductionVariable(const Loop *L, ScalarEvolution &SE);
  Optional<Loop::LoopBounds> getBounds(const Loop *L, ScalarEvolution &SE);
  bool getInductionDescriptor(const Loop *L, ScalarEvolution &SE,
                              InductionDescriptor &IndDesc);

} // namespace liberty

#endif
