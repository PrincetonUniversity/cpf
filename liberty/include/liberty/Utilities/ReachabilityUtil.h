#ifndef REACHABILITY_UTIL_H
#define REACHABILITY_UTIL_H

#include "llvm/IR/Instructions.h"

#include "liberty/Utilities/ModuleLoops.h"

#include <vector>
#include <unordered_map>

using namespace llvm;

namespace liberty {

typedef std::vector<const Instruction *> RelInstsV;
typedef std::unordered_map<const Function *, RelInstsV> Fun2RelInsts;

bool isReachableIntraprocedural(const Instruction *src, const Instruction *dst,
                                ModuleLoops &mloops);
bool isReachableIntraprocedural(RelInstsV &srcInsts, const Instruction *dst,
                                ModuleLoops &mloops);

Fun2RelInsts populateFun2RelInsts(const Instruction *I);

bool isReachableInterProcedural(Fun2RelInsts &fun2RelInsts,
                                const Instruction *dst, ModuleLoops &mloops);
bool isReachableInterProcedural(const Instruction *src,
                                std::vector<const Instruction *> dsts,
                                ModuleLoops &mloops);

bool noStoreInBetween(const Instruction *firstI, const Instruction *secondI,
                      std::vector<const Instruction *> &defs,
                      ModuleLoops &mloops);

} // namespace liberty

#endif
