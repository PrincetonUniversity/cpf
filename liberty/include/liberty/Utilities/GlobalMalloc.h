#ifndef GLOBAL_MALLOC_UTIL_H
#define GLOBAL_MALLOC_UTIL_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"

#include <vector>

using namespace llvm;

namespace liberty {

// returns true if global is not captured.
// populates srcs with all the defs of the global.
bool findNoCaptureGlobalSrcs(const GlobalValue *global,
                             std::vector<const Instruction *> &srcs);

// returns true if global is not captured and has only no-alias sources.
// populates mallocSrcs with all the no-alias sources.
bool findNoCaptureGlobalMallocSrcs(const GlobalValue *global,
                                   std::vector<const Instruction *> &mallocSrcs,
                                   const TargetLibraryInfo *tli);

void findAllocSizeInfo(const Instruction *alloc, const Value **numOfElem,
                       uint64_t &sizeOfElem);

bool isGlobalLocalToLoop(const GlobalValue *gv, const Loop *L);

} // namespace liberty

#endif
