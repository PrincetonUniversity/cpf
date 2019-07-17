#ifndef GEP_RANGE_UTIL_H
#define GEP_RANGE_UTIL_H

#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace liberty {

const Value *getCanonicalGepRange(const GetElementPtrInst *gep, const Loop *L,
                                  ScalarEvolution *se);

const Value *getCanonicalRange(const SCEVAddRecExpr *addRec, const Loop *L,
                               ScalarEvolution *se);

bool hasLoopInvariantTripCountUnknownSCEV(const Loop *L);
const Value *getLimitUnknown(const Value *idx, const Loop *L);

const Value *bypassExtInsts(const Value *v);

} // namespace liberty

#endif
