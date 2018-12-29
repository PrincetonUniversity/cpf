#ifndef FIND_ALL_TRANS_USES_H
#define FIND_ALL_TRANS_USES_H

#include "llvm/IR/Value.h"
#include "llvm/ADT/DenseSet.h"

namespace liberty {
  void findAllTransUses(const llvm::Value *V,
                        llvm::DenseSet<const llvm::Value *> &uses);
}

#endif /* FIND_ALL_TRANS_USES_H */
