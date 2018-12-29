#ifndef LLVM_LIBERTY_UTILS_INLINE_FUNCTION_WITH_VMAP_H
#define LLVM_LIBERTY_UTILS_INLINE_FUNCTION_WITH_VMAP_H

#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/CallSite.h"

namespace liberty
{
using namespace llvm;

typedef std::vector<const InvokeInst *> CallsPromotedToInvoke;


/// Similar to llvm::InlineFunction except return a vmap.
/// Consequently, it must perform fewer simplifications during inlining.
bool InlineFunctionWithVmap(
  CallSite CS,
  InlineFunctionInfo &IFI,
  ValueToValueMapTy &vmap, // output
  CallsPromotedToInvoke &call2invoke, // output
  bool InsertLifetime = true);
}

#endif

