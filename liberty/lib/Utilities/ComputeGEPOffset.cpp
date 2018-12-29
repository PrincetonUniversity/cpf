#include "liberty/Utilities/ComputeGEPOffset.h"

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

namespace liberty
{

using namespace llvm;

static Value *GetLinearExpression(Value *V, APInt &Scale, APInt &Offset,
                                  ExtensionKind &Extension,
                                  const DataLayout &TD, unsigned Depth) {
  assert(V->getType()->isIntegerTy() && "Not an integer value");

  // Limit our recursion depth.
  if (Depth == 6) {
    Scale = 1;
    Offset = 0;
    return V;
  }

  if (BinaryOperator *BOp = dyn_cast<BinaryOperator>(V)) {
    if (ConstantInt *RHSC = dyn_cast<ConstantInt>(BOp->getOperand(1))) {
      switch (BOp->getOpcode()) {
      default: break;
      case Instruction::Or:
        // X|C == X+C if all the bits in C are unset in X.  Otherwise we can't
        // analyze it.
        //sot
        //if (!MaskedValueIsZero(BOp->getOperand(0), RHSC->getValue(), &TD))
        if (!MaskedValueIsZero(BOp->getOperand(0), RHSC->getValue(), TD))
          break;
        // FALL THROUGH.
      case Instruction::Add:
        V = GetLinearExpression(BOp->getOperand(0), Scale, Offset, Extension,
                                TD, Depth+1);
        Offset += RHSC->getValue();
        return V;
      case Instruction::Mul:
        V = GetLinearExpression(BOp->getOperand(0), Scale, Offset, Extension,
                                TD, Depth+1);
        Offset *= RHSC->getValue();
        Scale *= RHSC->getValue();
        return V;
      case Instruction::Shl:
        V = GetLinearExpression(BOp->getOperand(0), Scale, Offset, Extension,
                                TD, Depth+1);
        Offset <<= RHSC->getValue().getLimitedValue();
        Scale <<= RHSC->getValue().getLimitedValue();
        return V;
      }
    }
  }

  // Since GEP indices are sign extended anyway, we don't care about the high
  // bits of a sign or zero extended value - just scales and offsets.  The
  // extensions have to be consistent though.
  if ((isa<SExtInst>(V) && Extension != EK_ZeroExt) ||
      (isa<ZExtInst>(V) && Extension != EK_SignExt)) {
    Value *CastOp = cast<CastInst>(V)->getOperand(0);
    unsigned OldWidth = Scale.getBitWidth();
    unsigned SmallWidth = CastOp->getType()->getPrimitiveSizeInBits();
    Scale = Scale.trunc(SmallWidth);
    Offset = Offset.trunc(SmallWidth);
    Extension = isa<SExtInst>(V) ? EK_SignExt : EK_ZeroExt;

    Value *Result = GetLinearExpression(CastOp, Scale, Offset, Extension,
                                        TD, Depth+1);
    Scale = Scale.zext(OldWidth);
    Offset = Offset.zext(OldWidth);

    return Result;
  }

  Scale = 1;
  Offset = 0;
  return V;
}

int64_t computeOffset(GetElementPtrInst* GEPOp, const DataLayout* TD) {
  int64_t                          BaseOffs = 0;
  SmallVector<VariableGEPIndex, 8> VarIndices;

  gep_type_iterator GTI = gep_type_begin(GEPOp);
  for (User::const_op_iterator I = GEPOp->op_begin()+1,
      E = GEPOp->op_end(); I != E; ++I) {
    Value *Index = *I;
    // Compute the (potentially symbolic) offset in bytes for this index.
    //if (StructType *STy = dyn_cast<StructType>(*GTI++)) {
    if (StructType *STy = GTI.getStructTypeOrNull()) {
      // For a struct, add the member offset.
      unsigned FieldNo = cast<ConstantInt>(Index)->getZExtValue();
      if (FieldNo == 0) continue;

      BaseOffs += TD->getStructLayout(STy)->getElementOffset(FieldNo);
      continue;
    }

    // For an array/pointer, add the element offset, explicitly scaled.
    if (ConstantInt *CIdx = dyn_cast<ConstantInt>(Index)) {
      if (CIdx->isZero()) continue;
      BaseOffs += TD->getTypeAllocSize(GTI.getIndexedType())*CIdx->getSExtValue();
      continue;
    }

    uint64_t Scale = TD->getTypeAllocSize(GTI.getIndexedType());
    ExtensionKind Extension = EK_NotExtended;

    // If the integer type is smaller than the pointer size, it is implicitly
    // sign extended to pointer size.
    unsigned Width = cast<IntegerType>(Index->getType())->getBitWidth();
    if (TD->getPointerSizeInBits() > Width)
      Extension = EK_SignExt;

    // Use GetLinearExpression to decompose the index into a C1*V+C2 form.
    APInt IndexScale(Width, 0), IndexOffset(Width, 0);
    Index = GetLinearExpression(Index, IndexScale, IndexOffset, Extension,
        *TD, 0);

    // The GEP index scale ("Scale") scales C1*V+C2, yielding (C1*V+C2)*Scale.
    // This gives us an aggregate computation of (C1*Scale)*V + C2*Scale.
    BaseOffs += IndexOffset.getZExtValue()*Scale;
    Scale *= IndexScale.getZExtValue();


    // If we already had an occurrance of this index variable, merge this
    // scale into it.  For example, we want to handle:
    //   A[x][x] -> x*16 + x*4 -> x*20
    // This also ensures that 'x' only appears in the index list once.
    for (unsigned i = 0, e = VarIndices.size(); i != e; ++i) {
      if (VarIndices[i].V == Index &&
          VarIndices[i].Extension == Extension) {
        Scale += VarIndices[i].Scale;
        VarIndices.erase(VarIndices.begin()+i);
        break;
      }
    }

    // Make sure that we have a scale that makes sense for this target's
    // pointer size.
    if (unsigned ShiftBits = 64-TD->getPointerSizeInBits()) {
      Scale <<= ShiftBits;
      Scale >>= ShiftBits;
    }

    if (Scale) {
      VariableGEPIndex Entry = {Index, Extension, Scale};
      VarIndices.push_back(Entry);
    }
  }

  return BaseOffs;
}

}
