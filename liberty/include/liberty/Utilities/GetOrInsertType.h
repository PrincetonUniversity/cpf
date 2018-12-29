#ifndef GET_OR_INSERT_TYPE_H
#define GET_OR_INSERT_TYPE_H

#include "llvm/IR/Module.h"

namespace liberty {
  llvm::PointerType *getPointerTy(llvm::Type *type);
  llvm::PointerType *getOrInsertType(llvm::Module &mod, llvm::StringRef name);
}

#endif /* GET_OR_INSERT_TYPE_H */
