#ifndef GET_MEM_OPER
#define GET_MEM_OPER

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"

namespace liberty {
  const llvm::Value *getMemOper(const llvm::Instruction *inst);
  llvm::Value *getMemOper(llvm::Instruction *inst);
  void setMemOper(llvm::Instruction *inst, llvm::Value *value);
}

#endif /* GET_MEM_OPER */
