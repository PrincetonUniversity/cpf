#ifndef ALLOCA_HACKS_H
#define ALLOCA_HACKS_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/User.h"

namespace liberty {

  using namespace llvm;

  // N.B. We use alloca's to avoid the difficulties
  // associated with SSA.  These difficulties are
  // compounded by the fact that the CFG may be
  // in an intermediate state.
  //
  // Because we use these allocas in a restricted fashion,
  // Running mem2reg later can eliminate every single alloca we
  // create.


  // Replace every use of the value <value> that occurs within
  // the function <scope> with a carefully placed load instruction
  // from the memory location <alloca>  This function is aware
  // of PHI nodes and the CFG at large.  It assumes that the
  // CFG structure is sane, even if the instructions inside may
  // be completely insane.
  void replaceUsesWithinFcnWithLoadFromAlloca(Value *oldValue, Function *scope, Value *alloca);

  // Replace every use of the value <oldValue> that occurs within
  // the function <scope> with a use of <newValue>
  void replaceUsesWithinFcn( Value *oldValue, Function *scope, Value *newValue );


}

#endif

