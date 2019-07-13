#ifndef REACHABILITY_UTIL_H
#define REACHABILITY_UTIL_H

#include "llvm/IR/Instructions.h"

#include "liberty/Utilities/ModuleLoops.h"

#include <vector>

using namespace llvm;

namespace liberty {

bool noStoreInBetween(const Instruction *firstI, const Instruction *secondI,
                      std::vector<const Instruction *> &defs,
                      ModuleLoops &mloops);

} // namespace liberty

#endif
