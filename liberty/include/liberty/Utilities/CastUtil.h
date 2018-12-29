#ifndef CAST_UTIL_H
#define CAST_UTIL_H

#include <vector>
#include "llvm/IR/Instruction.h"
#include "liberty/Utilities/InstInsertPt.h"

namespace liberty {

  typedef std::vector<llvm::Instruction*> NewInstructions;

  llvm::Value *castToInt64Ty(llvm::Value *value,
                             liberty::InstInsertPt &out,
                             NewInstructions *newInstructions = 0);

  llvm::Value *castIntToInt32Ty(llvm::Value *value,
                             liberty::InstInsertPt &out,
                             NewInstructions *newInstructions = 0);

  llvm::Value *castFromInt64Ty(llvm::Type *ty, llvm::Value *value,
                               liberty::InstInsertPt &out,
                               NewInstructions *newInstructions = 0);

  llvm::Value *castPtrToVoidPtr(llvm::Value *value, liberty::InstInsertPt &out,
                               NewInstructions *newInstructions = 0);
}

#endif /* CAST_UTIL_H */
