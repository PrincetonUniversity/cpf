#ifndef IS_INDEPENDANT_H
#define IS_INDEPENDANT_H

#include "llvm/IR/Instruction.h"


namespace liberty {

  bool isIndependent(const llvm::Instruction *I1,
                     const llvm::Instruction *I2);
}

#endif /* IS_INDEPENDANT_H */
