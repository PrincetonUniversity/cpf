#ifndef LLVM_LIBERTY_UTILITIES_GET_CIV_H
#define LLVM_LIBERTY_UTILITIES_GET_CIV_H

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"

namespace liberty
{
using namespace llvm;

PHINode *getOrInsertCanonicalInductionVariable(const Loop *loop);
}

#endif

