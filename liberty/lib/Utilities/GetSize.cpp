#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Utilities/GetSize.h"

using namespace llvm;

namespace liberty {

  unsigned getSize(Type *type, const DataLayout *TD) {
    if(type->isSized()) {
      return TD ? TD->getTypeStoreSize(type) : ~0u;
    }
    return 0;
  }

  // Conservatively return the size of a value
  unsigned getSize(const Value *value, const DataLayout *TD) {
    Type *type = value->getType();
    return getSize(type, TD);
  }

  unsigned getTargetSize(const Value *value, const DataLayout *TD) {
    Type *type = value->getType();

    Type *targetType;
    SequentialType *seqType = dyn_cast<SequentialType>(type);
    if (seqType)
      targetType = seqType->getElementType();
    else {
      PointerType *pType = dyn_cast<PointerType>(type);
      assert(pType && "Must be a SequentialType or PointerType");
      targetType = pType->getElementType();
    }

    return getSize(targetType, TD);
  }
}
