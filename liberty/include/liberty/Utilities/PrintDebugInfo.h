#ifndef PRINT_DEBUG_INFO_UTIL_H
#define PRINT_DEBUG_INFO_UTIL_H

#include "llvm/IR/Instruction.h"

namespace liberty {
void printInstDebugInfo(llvm::Instruction *I);
}

#endif /* PRINT_DEBUG_INFO_UTIL_H */
