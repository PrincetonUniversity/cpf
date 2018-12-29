#ifndef COMPUTE_GEP_OFFSET_H
#define COMPUTE_GEP_OFFSET_H

#include "llvm/IR/DataLayout.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instructions.h"

namespace liberty
{
  using namespace llvm;

  namespace {
    enum ExtensionKind {
      EK_NotExtended,
      EK_SignExt,
      EK_ZeroExt
    };
  
    struct VariableGEPIndex {
      const Value *V;
      ExtensionKind Extension;
      int64_t Scale;
  
      bool operator==(const VariableGEPIndex &Other) const {
        return V == Other.V && Extension == Other.Extension &&
          Scale == Other.Scale;
      }
  
      bool operator!=(const VariableGEPIndex &Other) const {
        return !operator==(Other);
      }
    };
  }
  
  int64_t computeOffset(GetElementPtrInst* GEPOp, const DataLayout* TD);
}

#endif /* COMPUTE_GEP_OFFSET */
