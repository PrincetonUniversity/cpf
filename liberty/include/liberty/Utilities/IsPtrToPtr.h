#ifndef IS_PTR_TO_PTR_H
#define IS_PTR_TO_PTR_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Analysis/LoopInfo.h"

namespace liberty {
  bool isPtrToPtr(llvm::Type *type);
  bool isInfinitePtr(const llvm::LoadInst *load);
  unsigned getPointerDepth(const llvm::LoadInst *load,
                           const llvm::Loop *loop);
}

#endif /* IS_PTR_TO_PTR_H */
