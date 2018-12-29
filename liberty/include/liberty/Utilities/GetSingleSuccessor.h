#ifndef GET_SINGLE_SUCCESSOR_H
#define GET_SINGLE_SUCCESSOR_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"

namespace liberty {
  llvm::BasicBlock *getSingleSuccessor(const llvm::BasicBlock *bb);
}

#endif /* GET_SINGLE_SUCCESSOR_H */
