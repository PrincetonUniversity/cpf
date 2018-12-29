#include "llvm/IR/Constants.h"

namespace liberty {
  void replaceGlobalWith(llvm::GlobalVariable *oldGlobal,
                         llvm::GlobalVariable *newGlobal);
}
