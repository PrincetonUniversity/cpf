#ifndef IS_VOLATILE_H
#define IS_VOLATILE_H

#include "llvm/IR/Instructions.h"

namespace liberty {
  bool isVolatile(const llvm::Instruction *inst);
}

#endif /* IS_VOLATILE_H */
