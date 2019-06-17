#ifndef FIND_SOURCE_H
#define FIND_SOURCE_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"

namespace liberty {
  const llvm::Value *findSource(const llvm::BinaryOperator *binop);
  const llvm::Value *findSource(const llvm::Instruction *i);
  const llvm::Value *findSource(const llvm::CallInst *call);
  const llvm::Value *findSource(const llvm::Value *v);
  const llvm::Value *findActualArgumentSource(const llvm::Value *v);
  const llvm::Argument *findArgumentSource(const llvm::Value *v);
  const llvm::Instruction *findNoAliasSource(const llvm::StoreInst *store,
                                             const llvm::TargetLibraryInfo &tli);
  const llvm::Instruction *findNoAliasSource(const llvm::Value *v,
                                             const llvm::TargetLibraryInfo &tli);
  const llvm::AllocaInst *findAllocaSource(const llvm::Value *v);
  const llvm::Value *findOffsetSource(const llvm::Value *v);
  const llvm::Argument *findLoadedNoCaptureArgument(const llvm::Value *v,
                                                    const llvm::DataLayout &DL);
  const llvm::Value *findDynSource(const llvm::Value *v);

  llvm::GlobalValue *findGlobalSource(const llvm::Value *v);

  llvm::Type *findDestinationType(const llvm::Value *v);
}

#endif /* FIND_SOURCE_H */
