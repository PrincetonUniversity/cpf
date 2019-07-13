#define DEBUG_TYPE "global-malloc-util"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/FindSource.h"
#include "liberty/Utilities/CaptureUtil.h"

#include <vector>
#include <cmath>

using namespace llvm;
using namespace liberty;

typedef Module::const_global_iterator GlobalIt;
typedef Value::const_user_iterator UseIt;

bool storeNull(const StoreInst *sI) {
  const Value *stValOp = sI->getValueOperand();
  if (PointerType *stValOpPtrTy = dyn_cast<PointerType>(stValOp->getType())) {
    auto nullPtrVal = ConstantPointerNull::get(cast<PointerType>(stValOpPtrTy));
    if (sI->getValueOperand() == nullPtrVal)
      return true;
  }
  return false;
}

bool findNoCaptureGlobalSrcs(const GlobalValue *global,
                             std::vector<const Instruction *> &srcs) {
  for (UseIt use = global->user_begin(); use != global->user_end(); ++use) {
    if (const StoreInst *store = dyn_cast<StoreInst>(*use)) {
      srcs.push_back(store);
    } else if (const BitCastOperator *bcOp = dyn_cast<BitCastOperator>(*use)) {
      for (UseIt bUse = bcOp->user_begin(); bUse != bcOp->user_end(); ++bUse) {
        if (const StoreInst *bStore = dyn_cast<StoreInst>(*bUse)) {
          srcs.push_back(bStore);
        } else if (!isa<LoadInst>(*bUse)) {
          // nonMalloc.insert(&*global);
          return false;
        }
      }
    } else if (!isa<LoadInst>(*use)) {
      // nonMalloc.insert(&*global);
      return false;
    }
  }
  return true;
}

bool findNoCaptureGlobalMallocSrcs(const GlobalValue *global,
                                   std::vector<const Instruction *> &mallocSrcs,
                                   const TargetLibraryInfo *tli) {
  Type *type = global->getType()->getElementType();
  if (!type->isPointerTy())
    return false;
  std::vector<const Instruction *> srcs;
  if (!findNoCaptureGlobalSrcs(global, srcs))
    return false;

  for (auto storeI : srcs) {
    const StoreInst *store = dyn_cast<StoreInst>(storeI);
    const Instruction *s = liberty::findNoAliasSource(store, *tli);
    if (s) {
      mallocSrcs.push_back(s);
    } else {
      // null is stored usually after a free operation.
      // Storing null should not be considered nonMalloc.
      // Undef behavior if the pointer is used with null value.
      // So we can leverage that and increase GlobalMallocAA
      // applicability
      if (storeNull(store))
        continue;
      // nonMalloc.insert(&*global);
      // nonMallocSrcs[&*global].insert(bStore);
      return false;
    }
  }
  return true;
}

void findAllocSizeInfo(const Instruction *alloc, const Value **numOfElem,
                       uint64_t &sizeOfElem) {

  sizeOfElem = 0;
  *numOfElem = nullptr;
  if (auto allocCall = dyn_cast<CallInst>(alloc)) {
    if (allocCall->getNumArgOperands() != 1)
      return;
    const Value *mallocArg = allocCall->getArgOperand(0);
    if (const Instruction *mallocSizeI = dyn_cast<Instruction>(mallocArg)) {
      if (auto binMallocSizeI = dyn_cast<BinaryOperator>(mallocSizeI)) {
        const ConstantInt *sizeOfElemInMalloc = nullptr;
        const Value *N = nullptr;
        if (isa<ConstantInt>(binMallocSizeI->getOperand(0))) {
          sizeOfElemInMalloc =
              dyn_cast<ConstantInt>(binMallocSizeI->getOperand(0));
          N = binMallocSizeI->getOperand(1);
        } else if (isa<ConstantInt>(binMallocSizeI->getOperand(1))) {
          sizeOfElemInMalloc =
              dyn_cast<ConstantInt>(binMallocSizeI->getOperand(1));
          N = binMallocSizeI->getOperand(0);
        }
        if (!sizeOfElemInMalloc)
          return;

        if (mallocSizeI->getOpcode() == Instruction::Shl) {
          sizeOfElem = pow(2, sizeOfElemInMalloc->getZExtValue());
          *numOfElem = N;
        } else if (mallocSizeI->getOpcode() == Instruction::Mul) {
          sizeOfElem = sizeOfElemInMalloc->getZExtValue();
          *numOfElem = N;
        }
        
      } else if (auto constAllocSize = dyn_cast<ConstantInt>(mallocSizeI)) {
        *numOfElem = nullptr;
        sizeOfElem = constAllocSize->getZExtValue();
      }
    }
  }
}

