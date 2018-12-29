#ifndef LLVM_LIBERTY_UTILITIES_REPLACE_CONSTANT_WITH_LOAD_H
#define LLVM_LIBERTY_UTILITIES_REPLACE_CONSTANT_WITH_LOAD_H

#include "llvm/IR/Constants.h"

namespace liberty
{
using namespace llvm;

/// Allows the caller to track new instructions caused by this transformation.
struct ReplaceConstantObserver
{
  virtual ~ReplaceConstantObserver() {};

  virtual void addInstruction(Instruction *newInst, Instruction *gravity) {};
};

/// Replace all uses of this constant with a load from the pointer.
/// This is non-trivial, since the constant may be used within
/// ConstantExpr objects.  If loadOncePerFcn is true, then at most
/// one load instruction will be inserted per function; otherwise,
/// it will be loaded before each use.
/// If successful, return true.  If it fails, then the IR will
/// be in an unpredictable state.
bool replaceConstantWithLoad(Constant *constant, Value *ptr, ReplaceConstantObserver &observer, bool loadOncePerFcn = true);

bool replaceConstantWithLoad(Constant *constant, Value *ptr, bool loadOncePerFcn = true);

}

#endif

