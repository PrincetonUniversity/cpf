#ifndef GEP_RANGE_UTIL_H
#define GEP_RANGE_UTIL_H

#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace liberty {

const Value *getCanonicalGepRange(const GetElementPtrInst *gep, const Loop *L,
                                  ScalarEvolution *se);

} // namespace liberty

#endif
