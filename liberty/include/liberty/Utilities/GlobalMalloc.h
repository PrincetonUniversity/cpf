#ifndef GLOBAL_MALLOC_UTIL_H
#define GLOBAL_MALLOC_UTIL_H

#include "llvm/IR/GlobalValue.h"

#include <vector>

using namespace llvm;

namespace liberty {

// return true if global is not captured and has only no alias sources
bool findNoCaptureGlobalMallocSrcs(const GlobalValue *global,
                                 std::vector<const Instruction *> &mallocSrcs);

void findAllocSizeInfo(const Instruction *alloc, const Value **numOfElem,
                       uint64_t &sizeOfElem);
}

#endif
