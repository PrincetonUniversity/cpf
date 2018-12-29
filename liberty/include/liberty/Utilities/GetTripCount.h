#ifndef GET_TRIP_COUNT
#define GET_TRIP_COUNT

#include "llvm/Analysis/LoopInfo.h"

namespace liberty {
  llvm::Value *getSimpleTripCount(const llvm::Loop *loop);
  llvm::Value *getTripCount(const llvm::Loop *loop);
}

#endif /* GET_TRIP_COUNT */
