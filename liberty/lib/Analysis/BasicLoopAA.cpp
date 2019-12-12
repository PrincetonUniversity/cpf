//===- BasicAliasAnalysis.cpp - Local Alias Analysis Impl -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the default implementation of the Alias Analysis interface
// that simply implements a few identities (two different globals cannot alias,
// etc), but otherwise does no analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/GetSize.h"
#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "llvm/Transforms/IPO/Attributor.h"

#include <algorithm>
using namespace llvm;
using namespace liberty;

//===----------------------------------------------------------------------===//
// Useful predicates
//===----------------------------------------------------------------------===//

/// isNonEscapingLocalObject - Return true if the pointer is to a function-local
/// object that never escapes from the function.
static bool isNonEscapingLocalObject(const Value *V) {
  // If this is a local allocation, check to see if it escapes.
  if (isa<AllocaInst>(V) || isNoAliasCall(V))
    // Set StoreCaptures to True so that we can assume in our callers that the
    // pointer is not the result of a load instruction. Currently
    // PointerMayBeCaptured doesn't have any special analysis for the
    // StoreCaptures=false case; if it did, our callers could be refined to be
    // more precise.
    return !PointerMayBeCaptured(V, false, /*StoreCaptures=*/true);

  // If this is an argument that corresponds to a byval or noalias argument,
  // then it has not escaped before entering the function.  Check if it escapes
  // inside the function.
  if (const Argument *A = dyn_cast<Argument>(V))
    if (A->hasByValAttr() || A->hasNoAliasAttr()) {
      // Don't bother analyzing arguments already known not to escape.
      if (A->hasNoCaptureAttr())
        return true;
      return !PointerMayBeCaptured(V, false, /*StoreCaptures=*/true);
    }
  return false;
}

/// isEscapeSource - Return true if the pointer is one which would have been
/// considered an escape by isNonEscapingLocalObject.
static bool isEscapeSource(const Value *V) {
  if (isa<CallInst>(V) || isa<InvokeInst>(V) || isa<Argument>(V))
    return true;

  // The load case works because isNonEscapingLocalObject considers all stores
  // to be escapes (it passes true for the StoreCaptures argument to
  // PointerMayBeCaptured).
  if (isa<LoadInst>(V))
    return true;

  return false;
}


/// Returns the size of the object specified by V or UnknownSize if unknown.
static uint64_t getObjectSize(const Value *V, const DataLayout &DL,
                              const TargetLibraryInfo &TLI,
                              bool RoundToAlign = false) {
  uint64_t Size;
  ObjectSizeOpts Opts;
  Opts.RoundToAlign = RoundToAlign;
  if (getObjectSize(V, Size, DL, &TLI, Opts))
    return Size;
  return MemoryLocation::UnknownSize;
}

/// Returns true if we can prove that the object specified by V is smaller than
/// Size.
static bool isObjectSmallerThan(const Value *V, uint64_t Size,
                                const DataLayout &DL,
                                const TargetLibraryInfo &TLI) {
  // Note that the meanings of the "object" are slightly different in the
  // following contexts:
  //    c1: llvm::getObjectSize()
  //    c2: llvm.objectsize() intrinsic
  //    c3: isObjectSmallerThan()
  // c1 and c2 share the same meaning; however, the meaning of "object" in c3
  // refers to the "entire object".
  //
  //  Consider this example:
  //     char *p = (char*)malloc(100)
  //     char *q = p+80;
  //
  //  In the context of c1 and c2, the "object" pointed by q refers to the
  // stretch of memory of q[0:19]. So, getObjectSize(q) should return 20.
  //
  //  However, in the context of c3, the "object" refers to the chunk of memory
  // being allocated. So, the "object" has 100 bytes, and q points to the middle
  // the "object". In case q is passed to isObjectSmallerThan() as the 1st
  // parameter, before the llvm::getObjectSize() is called to get the size of
  // entire object, we should:
  //    - either rewind the pointer q to the base-address of the object in
  //      question (in this case rewind to p), or
  //    - just give up. It is up to caller to make sure the pointer is pointing
  //      to the base address the object.
  //
  // We go for 2nd option for simplicity.
  if (!isIdentifiedObject(V))
    return false;

  // This function needs to use the aligned object size because we allow
  // reads a bit past the end given sufficient alignment.
  uint64_t ObjectSize = getObjectSize(V, DL, TLI, /*RoundToAlign*/ true);

  return ObjectSize != MemoryLocation::UnknownSize && ObjectSize < Size;
}

//===----------------------------------------------------------------------===//
// GetElementPtr Instruction Decomposition and Analysis
//===----------------------------------------------------------------------===//

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
  };
}


/// GetLinearExpression - Analyze the specified value as a linear expression:
/// "A*V + B", where A and B are constant integers.  Return the scale and offset
/// values as APInts and return V as a Value*, and return whether we looked
/// through any sign or zero extends.  The incoming Value is known to have
/// IntegerType and it may already be sign or zero extended.
///
/// Note that this looks through extends, so the high bits may not be
/// represented in the result.
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

/// DecomposeGEPExpression - If V is a symbolic pointer expression, decompose it
/// into a base pointer with a constant offset and a number of scaled symbolic
/// offsets.
///
/// The scaled symbolic offsets (represented by pairs of a Value* and a scale in
/// the VarIndices vector) are Value*'s that are known to be scaled by the
/// specified amount, but which may have other unrepresented high bits. As such,
/// the gep cannot necessarily be reconstructed from its decomposed form.
///
/// When DataLayout is around, this function is capable of analyzing everything
/// that Value::getUnderlyingObject() can look through.  When not, it just looks
/// through pointer casts.
///
static const Value *
DecomposeGEPExpression(const Value *V, int64_t &BaseOffs,
                       SmallVectorImpl<VariableGEPIndex> &VarIndices,
                       const DataLayout *TD) {

  // Limit recursion depth to limit compile time in crazy cases.
  unsigned MaxLookup = 6;

  BaseOffs = 0;
  do {
    // See if this is a bitcast or GEP.
    const Operator *Op = dyn_cast<Operator>(V);
    if (Op == 0) {
      // The only non-operator case we can handle are GlobalAliases.
      if (const GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
        if (!GA->isInterposable()) {
          V = GA->getAliasee();
          continue;
        }
      }
      return V;
    }

    if (Op->getOpcode() == Instruction::BitCast) {
      V = Op->getOperand(0);
      continue;
    }

    if (const PHINode *phi = dyn_cast<PHINode>(Op)) {

      const Value *first = phi->getIncomingValue(0);

      for (unsigned i = 1; i < phi->getNumIncomingValues(); ++i) {
        if (phi->getIncomingValue(i) != first)
          return V;
      }

      V = Op->getOperand(0);
      continue;
    }

    const GEPOperator *GEPOp = dyn_cast<GEPOperator>(Op);
    if (GEPOp == 0)
      return V;

    // Don't attempt to analyze GEPs over unsized objects.
    if (!GEPOp->getSourceElementType()->isSized())
    //if (!cast<PointerType>(GEPOp->getOperand(0)->getType())
    //    ->getElementType()->isSized())
      return V;

    // If we are lacking DataLayout information, we can't compute the offets of
    // elements computed by GEPs.  However, we can handle bitcast equivalent
    // GEPs.
    if (TD == 0) {
      if (!GEPOp->hasAllZeroIndices())
        return V;
      V = GEPOp->getOperand(0);
      continue;
    }

    // Walk the indices of the GEP, accumulating them into BaseOff/VarIndices.
    gep_type_iterator GTI = gep_type_begin(GEPOp);
    for (User::const_op_iterator I = GEPOp->op_begin()+1,
           E = GEPOp->op_end(); I != E; ++I, ++GTI) {
      Value *Index = *I;
      // Compute the (potentially symbolic) offset in bytes for this index.
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
        VariableGEPIndex Entry = {Index, Extension, static_cast<int64_t>(Scale)};
        VarIndices.push_back(Entry);
      }
    }

    // Analyze the base pointer next.
    V = GEPOp->getOperand(0);
  } while (--MaxLookup);

  // If the chain of expressions is too deep, just return early.
  return V;
}

/// GetIndexDifference - Dest and Src are the variable indices from two
/// decomposed GetElementPtr instructions GEP1 and GEP2 which have common base
/// pointers.  Subtract the GEP2 indices from GEP1 to find the symbolic
/// difference between the two pointers.
static void GetIndexDifference(SmallVectorImpl<VariableGEPIndex> &Dest,
                               const SmallVectorImpl<VariableGEPIndex> &Src) {
  if (Src.empty()) return;

  for (unsigned i = 0, e = Src.size(); i != e; ++i) {
    const Value *V = Src[i].V;
    ExtensionKind Extension = Src[i].Extension;
    int64_t Scale = Src[i].Scale;

    // Find V in Dest.  This is N^2, but pointer indices almost never have more
    // than a few variable indexes.
    for (unsigned j = 0, e = Dest.size(); j != e; ++j) {
      if (Dest[j].V != V || Dest[j].Extension != Extension) continue;

      // If we found it, subtract off Scale V's from the entry in Dest.  If it
      // goes to zero, remove the entry.
      if (Dest[j].Scale != Scale)
        Dest[j].Scale -= Scale;
      else
        Dest.erase(Dest.begin()+j);
      Scale = 0;
      break;
    }

    // If we didn't consume this entry, add it to the end of the Dest list.
    if (Scale) {
      VariableGEPIndex Entry = { V, Extension, -Scale };
      Dest.push_back(Entry);
    }
  }
}

//===----------------------------------------------------------------------===//
// BasicLoopAA Pass
//===----------------------------------------------------------------------===//

namespace {
  /// BasicLoopAA - This is the default alias analysis implementation.
  /// Because it doesn't chain to a previous alias analysis (like -no-aa), it
  /// derives from the LoopAA class.
  struct BasicLoopAA : public liberty::ClassicLoopAA, public ModulePass {
    static char ID; // Class identification, replacement for typeinfo
    BasicLoopAA() : ModulePass(ID) {}

    Module *currentMod;

    bool runOnModule(Module &M) {
      currentMod = &M;
      InitializeLoopAA(this, M.getDataLayout());
      return false;
    }

    virtual ModRefResult getModRefInfo(CallSite CS1, TemporalRelation Rel,
                                       CallSite CS2, const Loop *L,
                                       Remedies &R) {
      return ModRef;
    }

    virtual ModRefResult getModRefInfo(CallSite CS, TemporalRelation Rel,
                                       const Pointer &P2, const Loop *L,
                                       Remedies &R);

    virtual AliasResult
    aliasCheck(const Pointer &P1, TemporalRelation Rel, const Pointer &P2,
               const Loop *L, Remedies &R,
               DesiredAliasResult dAliasRes = DNoOrMustAlias);

    AliasResult aliasCommon(const Value *V1, unsigned V1Size,
                            TemporalRelation Rel, const Value *V2,
                            unsigned V2Size, const Loop *L, Remedies &R);

    /// pointsToConstantMemory - Chase pointers until we find a (constant
    /// global) or not.
    virtual bool pointsToConstantMemory(const Value *P, const Loop *L);

    StringRef getLoopAAName() const {
      return "basic-loop-aa";
    }

    virtual SchedulingPreference getSchedulingPreference() const {
      return Top;
    }

    /// getAdjustedAnalysisPointer - This method is used when a pass implements
    /// an analysis interface through multiple inheritance.  If needed, it
    /// should override this to adjust the this pointer as needed for the
    /// specified pass info.
    virtual void *getAdjustedAnalysisPointer(const void *ID) {
      if (ID == &LoopAA::ID)
        return (AliasAnalysis*)this;
      return this;
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      LoopAA::getAnalysisUsage(AU);
      AU.setPreservesAll();
    }

  private:
    // Visited - Track instructions visited by a aliasPHI, aliasSelect(), and aliasGEP().
    SmallPtrSet<const Value*, 16> Visited;

    // aliasGEP - Provide a bunch of ad-hoc rules to disambiguate a GEP
    // instruction against another.
    AliasResult aliasGEP(const GEPOperator *V1, unsigned V1Size,
                         TemporalRelation Rel, const Value *V2, unsigned V2Size,
                         const Loop *L, const Value *UnderlyingV1,
                         const Value *UnderlyingV2, Remedies &R);

    // aliasPHI - Provide a bunch of ad-hoc rules to disambiguate a PHI
    // instruction against another.
    AliasResult aliasPHI(const PHINode *PN, unsigned PNSize,
                         TemporalRelation Rel, const Value *V2, unsigned V2Size,
                         const Loop *L, Remedies &R);

    /// aliasSelect - Disambiguate a Select instruction against another value.
    AliasResult aliasSelect(const SelectInst *SI, unsigned SISize,
                            TemporalRelation Rel, const Value *V2,
                            unsigned V2Size, const Loop *L, Remedies &R);
  };
}  // End of anonymous namespace

// Register this pass...
char BasicLoopAA::ID = 0;
static RegisterPass<BasicLoopAA>
X("basic-loop-aa", "Basic Loop Alias Analysis", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

/// pointsToConstantMemory - Chase pointers until we find a (constant
/// global) or not.
bool BasicLoopAA::pointsToConstantMemory(const Value *P, const Loop *L) {
  if (const GlobalVariable *GV =
      dyn_cast<GlobalVariable>(GetUnderlyingObject(P, currentMod->getDataLayout())))
    // Note: this doesn't require GV to be "ODR" because it isn't legal for a
    // global to be marked constant in some modules and non-constant in others.
    // GV may even be a declaration, not a definition.
    return GV->isConstant();

  if(isa<Function>(P))
    return true;

  return LoopAA::pointsToConstantMemory(P, L);
}

/// modref - Check to see if the specified callsite can clobber the specified
/// memory object.  Since we only look at local properties of this function, we
/// really can't say much about this query.  We do, however, use simple "address
/// taken" analysis on local objects.
liberty::LoopAA::ModRefResult
BasicLoopAA::getModRefInfo(CallSite CS, TemporalRelation Rel, const Pointer &P2,
                           const Loop *L, Remedies &R) {

  const Value *V = P2.ptr;
  const unsigned Size = P2.size;

  if(isInterprocedural(CS.getInstruction(), V))
    return ModRef;

  // If P points to a constant memory location, the call definitely could not
  // modify the memory location.
  if(pointsToConstantMemory(V,L))
    return Ref;

  const Value *Object = GetUnderlyingObject(V, currentMod->getDataLayout());

  // If this is a tail call and P points to a stack location, we know that the
  // tail call cannot access or modify the local stack.  We cannot exclude byval
  // arguments here; these belong to the caller of the current function not to
  // the current function, and a tail callee may reference them.
  if (isa<AllocaInst>(Object))
    if (const CallInst *CI = dyn_cast<CallInst>(CS.getInstruction()))
      if (CI->isTailCall())
        return NoModRef;

  // If the pointer is to a locally allocated object that does not escape, then
  // the call cannot mod/ref the pointer unless the call takes the pointer as an
  // argument, and itself doesn't capture it.
  if (!isa<Constant>(Object) && CS.getInstruction() != Object &&
      isNonEscapingLocalObject(Object)) {
    bool PassedAsArg = false;
    unsigned ArgNo = 0;
    Remedies tmpR;
    for (ImmutableCallSite::arg_iterator CI = CS.arg_begin(), CE = CS.arg_end();
         CI != CE; ++CI, ++ArgNo) {
      // Only look at the no-capture pointer arguments.
      if (!(*CI)->getType()->isPointerTy() ||
          (!CS.doesNotCapture(ArgNo) && !CS.isByValArgument(ArgNo)))
          //!CS.paramHasAttr(ArgNo+1, Attribute::NoCapture))
        continue;

      // If this is a no-capture pointer argument, see if we can tell that it is
      // impossible to alias the pointer we're checking.  If not, we have to
      // assume that the call could touch the pointer, even though it doesn't
      // escape.
      if (!isNoAlias(cast<Value>(CI), UnknownSize, Rel, V, UnknownSize, L,
                     tmpR)) {
        PassedAsArg = true;
        break;
      }
    }

    if (!PassedAsArg) {
      for (auto remed : tmpR)
        R.insert(remed);
      return NoModRef;
    }
  }

  // Finally, handle specific knowledge of intrinsics.
  const IntrinsicInst *II = dyn_cast<IntrinsicInst>(CS.getInstruction());
  if (II != 0)
    switch (II->getIntrinsicID()) {
    default: break;
    case Intrinsic::memcpy:
    case Intrinsic::memmove: {
      unsigned Len = UnknownSize;
      if (ConstantInt *LenCI = dyn_cast<ConstantInt>(II->getArgOperand(2)))
        Len = LenCI->getZExtValue();
      Value *Dest = II->getArgOperand(0);
      Value *Src = II->getArgOperand(1);
      Remedies tmpR1, tmpR2;
      if (isNoAlias(Dest, Len, Rel, V, Size, L, tmpR1)) {
        if (isNoAlias(Src, Len, Rel, V, Size, L, tmpR2)) {
          for (auto remed : tmpR1)
            R.insert(remed);
          for (auto remed : tmpR2)
            R.insert(remed);
          return NoModRef;
        }
        for (auto remed : tmpR1)
          R.insert(remed);
        return Ref;
      }
      break;
    }
    case Intrinsic::memset:
      // Since memset is 'accesses arguments' only, the LoopAA base class
      // will handle it for the variable length case.
      if (ConstantInt *LenCI = dyn_cast<ConstantInt>(II->getArgOperand(2))) {
        unsigned Len = LenCI->getZExtValue();
        Value *Dest = II->getArgOperand(0);
        Remedies tmpR;
        if (isNoAlias(Dest, Len, Rel, V, Size, L, tmpR)) {
          for (auto remed : tmpR)
            R.insert(remed);
          return NoModRef;
        }
      }
      break;
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::invariant_start: {
      unsigned PtrSize =
        cast<ConstantInt>(II->getArgOperand(0))->getZExtValue();
      Remedies tmpR;
      if (isNoAlias(II->getArgOperand(1), PtrSize, Rel, V, Size, L, tmpR)) {
        for (auto remed : tmpR)
          R.insert(remed);
        return NoModRef;
      }
      break;
    }
    case Intrinsic::invariant_end: {
      unsigned PtrSize =
        cast<ConstantInt>(II->getArgOperand(1))->getZExtValue();
      Remedies tmpR;
      if (isNoAlias(II->getArgOperand(2), PtrSize, Rel, V, Size, L, tmpR)) {
        for (auto remed : tmpR)
          R.insert(remed);
        return NoModRef;
      }
      break;
    }
    }

  return ModRef;
}


/// aliasGEP - Provide a bunch of ad-hoc rules to disambiguate a GEP instruction
/// against another pointer.  We know that V1 is a GEP, but we don't know
/// anything about V2.  UnderlyingV1 is GEP1->getUnderlyingObject(),
/// UnderlyingV2 is the same for V2.
///
liberty::LoopAA::AliasResult
BasicLoopAA::aliasGEP(const GEPOperator *GEP1, unsigned V1Size,
                      TemporalRelation Rel, const Value *V2, unsigned V2Size,
                      const Loop *L, const Value *UnderlyingV1,
                      const Value *UnderlyingV2, Remedies &R) {

  // If this GEP has been visited before, we're on a use-def cycle.  Such cycles
  // are only valid when PHI nodes are involved or in unreachable code. The
  // visitPHI function catches cycles containing PHIs, but there could still be
  // a cycle without PHIs in unreachable code.
  if (!Visited.insert(GEP1).second)
    return MayAlias;

  int64_t GEP1BaseOffset;
  SmallVector<VariableGEPIndex, 4> GEP1VariableIndices;

  // If we have two gep instructions with must-alias'ing base pointers, figure
  // out if the indexes to the GEP tell us anything about the derived pointer.
  if (const GEPOperator *GEP2 = dyn_cast<GEPOperator>(V2)) {
    // Do the base pointers alias?
    AliasResult BaseAlias = aliasCommon(UnderlyingV1, UnknownSize,
                                        Rel,
                                        UnderlyingV2, UnknownSize,
                                        L, R);

    // If we get a No or May, then return it immediately, no amount of analysis
    // will improve this situation.
    if (BaseAlias != MustAlias )
    {
      return BaseAlias;
    }
    if( Rel != Same || !L )
    {
      if( BaseAlias == MustAlias )
        return MayAlias;
      else
        return BaseAlias;
    }

    // Otherwise, we have a MustAlias.  Since the base pointers alias each other
    // exactly, see if the computed offset from the common pointer tells us
    // about the relation of the resulting pointer.
    const DataLayout *TD = getDataLayout();
    const Value *GEP1BasePtr =
      DecomposeGEPExpression(GEP1, GEP1BaseOffset, GEP1VariableIndices, TD);

    int64_t GEP2BaseOffset;
    SmallVector<VariableGEPIndex, 4> GEP2VariableIndices;
    const Value *GEP2BasePtr =
      DecomposeGEPExpression(GEP2, GEP2BaseOffset, GEP2VariableIndices, TD);

    // If DecomposeGEPExpression isn't able to look all the way through the
    // addressing operation, we must not have TD and this is too complex for us
    // to handle without it.
    if (GEP1BasePtr != UnderlyingV1 || GEP2BasePtr != UnderlyingV2) {
      assert(TD == 0 &&
             "DecomposeGEPExpression and getUnderlyingObject disagree!");
      return MayAlias;
    }

    // Subtract the GEP2 pointer from the GEP1 pointer to find out their
    // symbolic difference.
    GEP1BaseOffset -= GEP2BaseOffset;
    GetIndexDifference(GEP1VariableIndices, GEP2VariableIndices);

  } else {
    // Check to see if these two pointers are related by the getelementptr
    // instruction.  If one pointer is a GEP with a non-zero index of the other
    // pointer, we know they cannot alias.

    // If both accesses are unknown size, we can't do anything useful here.
    if (V1Size == UnknownSize && V2Size == UnknownSize)
      return MayAlias;

    AliasResult Res = aliasCommon(UnderlyingV1, UnknownSize, Rel, V2, V2Size, L, R);
    if (Res != MustAlias)
      // If V2 may alias GEP base pointer, conservatively returns MayAlias.  If
      // V2 is known not to alias GEP base pointer, then the two values cannot
      // alias per GEP semantics: "A pointer value formed from a getelementptr
      // instruction is associated with the addresses associated with the first
      // operand of the getelementptr".
      return Res;

    const DataLayout *TD = getDataLayout();
    const Value *GEP1BasePtr =
      DecomposeGEPExpression(GEP1, GEP1BaseOffset, GEP1VariableIndices, TD);

    // If DecomposeGEPExpression isn't able to look all the way through the
    // addressing operation, we must not have TD and this is too complex for us
    // to handle without it.
    if (GEP1BasePtr != UnderlyingV1) {
      assert(TD == 0 &&
             "DecomposeGEPExpression and getUnderlyingObject disagree!");
      return MayAlias;
    }
  }

  // In the two GEP Case, if there is no difference in the offsets of the
  // computed pointers, the resultant pointers are a must alias.  This hapens
  // when we have two lexically identical GEP's (for example).
  //
  // In the other case, if we have getelementptr <ptr>, 0, 0, 0, 0, ... and V2
  // must aliases the GEP, the end result is a must alias also.
  if (GEP1BaseOffset == 0 && GEP1VariableIndices.empty())
    return MustAlias;

  // If we have a known constant offset, see if this offset is larger than the
  // access size being queried.  If so, and if no variable indices can remove
  // pieces of this constant, then we know we have a no-alias.  For example,
  // &A[1= &A.

  // In order to handle cases like &A[100][i] where i is an out of range
  // subscript, we have to ignore all constant offset pieces that are a multiple
  // of a scaled index.  Do this by removing constant offsets that are a
  // multiple of any of our variable indices.  This allows us to transform
  // things like &A[i][1] because i has a stride of (e.g.) 8 bytes but the 1
  // provides an offset of 4 bytes (assuming a <= 4 byte access).
  for (unsigned i = 0, e = GEP1VariableIndices.size();
       i != e && GEP1BaseOffset;++i)
    if (int64_t RemovedOffset = GEP1BaseOffset/GEP1VariableIndices[i].Scale)
      GEP1BaseOffset -= RemovedOffset*GEP1VariableIndices[i].Scale;

  // If our known offset is bigger than the access size, we know we don't have
  // an alias.
  if (GEP1BaseOffset) {
    if (GEP1BaseOffset >= (int64_t)V2Size ||
        GEP1BaseOffset <= -(int64_t)V1Size)
      return NoAlias;
  }

  return MayAlias;
}

/// aliasSelect - Provide a bunch of ad-hoc rules to disambiguate a Select
/// instruction against another.
liberty::LoopAA::AliasResult
BasicLoopAA::aliasSelect(const SelectInst *SI, unsigned SISize,
                         TemporalRelation Rel, const Value *V2, unsigned V2Size,
                         const Loop *L, Remedies &R) {
  // If this select has been visited before, we're on a use-def cycle.  Such
  // cycles are only valid when PHI nodes are involved or in unreachable
  // code. The visitPHI function catches cycles containing PHIs, but there could
  // still be a cycle without PHIs in unreachable code.
  if (!Visited.insert(SI).second)
    return MayAlias;

  // If the values are Selects with the same condition, we can do a more precise
  // check: just check for aliases between the values on corresponding arms.
  if(Rel == Same && L) {
    if (const SelectInst *SI2 = dyn_cast<SelectInst>(V2)) {
      if (SI->getCondition() == SI2->getCondition()) {
        AliasResult Alias =
          aliasCommon(SI->getTrueValue(), SISize,
                      Rel,
                      SI2->getTrueValue(), V2Size,
                      L, R);
        if (Alias == MayAlias)
          return MayAlias;
        AliasResult ThisAlias =
          aliasCommon(SI->getFalseValue(), SISize,
                      Rel,
                      SI2->getFalseValue(), V2Size,
                      L, R);
        if (ThisAlias != Alias)
          return MayAlias;
        return Alias;
      }
    }
  }

  // If both arms of the Select node NoAlias or MustAlias V2, then returns
  // NoAlias / MustAlias. Otherwise, returns MayAlias.
  AliasResult Alias =
    aliasCommon(V2, V2Size, Rel, SI->getTrueValue(), SISize, L, R);
  if (Alias == MayAlias)
    return MayAlias;

  // If V2 is visited, the recursive case will have been caught in the
  // above aliasCommon call, so these subsequent calls to aliasCommon
  // don't need to assume that V2 is being visited recursively.
  Visited.erase(V2);

  AliasResult ThisAlias =
    aliasCommon(V2, V2Size, Rel, SI->getFalseValue(), SISize, L, R);
  if (ThisAlias != Alias)
    return MayAlias;
  return Alias;
}

// aliasPHI - Provide a bunch of ad-hoc rules to disambiguate a PHI instruction
// against another.
liberty::LoopAA::AliasResult
BasicLoopAA::aliasPHI(const PHINode *PN, unsigned PNSize, TemporalRelation Rel,
                      const Value *V2, unsigned V2Size, const Loop *L,
                      Remedies &R) {
  // The PHI node has already been visited, avoid recursion any further.
  if (!Visited.insert(PN).second)
    return MayAlias;

  // If the values are PHIs in the same block, we can do a more precise as well
  // as efficient check: just check for aliases between the values on
  // corresponding edges.
  if (Rel == Same && L) {
    if (const PHINode *PN2 = dyn_cast<PHINode>(V2)) {
      if (PN2->getParent() == PN->getParent()) {
        AliasResult Alias =
          aliasCommon(PN->getIncomingValue(0), PNSize,
                      Rel,
                      PN2->getIncomingValueForBlock(PN->getIncomingBlock(0)),
                      V2Size,
                      L, R);
        if (Alias == MayAlias)
          return MayAlias;
        for (unsigned i = 1, e = PN->getNumIncomingValues(); i != e; ++i) {
          AliasResult ThisAlias =
            aliasCommon(PN->getIncomingValue(i), PNSize,
                        Rel,
                        PN2->getIncomingValueForBlock(PN->getIncomingBlock(i)),
                        V2Size,
                        L, R);
          if (ThisAlias != Alias)
            return MayAlias;
        }
        return Alias;
      }
    }
  }

  SmallPtrSet<Value*, 4> UniqueSrc;
  SmallVector<Value*, 4> V1Srcs;
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    Value *PV1 = PN->getIncomingValue(i);
    if (isa<PHINode>(PV1))
      // If any of the source itself is a PHI, return MayAlias conservatively to
      // avoid compile time explosion. The worst possible case is if both sides
      // are PHI nodes. In which case, this is O(m x n) time where 'm' and 'n'
      // are the number of PHI sources.
      return MayAlias;
    if (UniqueSrc.insert(PV1).second)
      V1Srcs.push_back(PV1);
  }

  AliasResult Alias = aliasCommon(V2, V2Size, Rel, V1Srcs[0], PNSize, L, R);
  // Early exit if the check of the first PHI source against V2 is MayAlias.
  // Other results are not possible.
  if (Alias == MayAlias)
    return MayAlias;

  // If all sources of the PHI node NoAlias or MustAlias V2, then returns
  // NoAlias / MustAlias. Otherwise, returns MayAlias.
  for (unsigned i = 1, e = V1Srcs.size(); i != e; ++i) {
    Value *V = V1Srcs[i];

    // If V2 is visited, the recursive case will have been caught in the above
    // aliasCommon call, so these subsequent calls to aliasCommon don't need to
    // assume that V2 is being visited recursively.
    Visited.erase(V2);

    AliasResult ThisAlias = aliasCommon(V2, V2Size, Rel, V, PNSize, L, R);
    if (ThisAlias != Alias || ThisAlias == MayAlias)
      return MayAlias;
  }

  return Alias;
}

// aliasCommon - Provide a bunch of ad-hoc rules to disambiguate in common cases,
// such as array references.
//
liberty::LoopAA::AliasResult
BasicLoopAA::aliasCheck(const Pointer &P1, TemporalRelation Rel,
                        const Pointer &P2, const Loop *L, Remedies &R,
                        DesiredAliasResult dAliasRes) {

  const Value *V1 = P1.ptr, *V2 = P2.ptr;
  const unsigned V1Size = P1.size, V2Size = P2.size;

  assert(Visited.empty() && "Visited must be cleared after use!");
  AliasResult AR = aliasCommon(V1, V1Size, Rel, V2, V2Size, L, R);
  Visited.clear();
  return AR;
}


liberty::LoopAA::AliasResult
BasicLoopAA::aliasCommon(const Value *V1, unsigned V1Size,
                         TemporalRelation Rel,
                         const Value *V2, unsigned V2Size,
                         const Loop *L, Remedies &R) {

  // This file is setup for intra-procedural analysis, if these two values
  // are from different functions we just give up right away
  if(isInterprocedural(V1, V2))
    return MayAlias;

  // If either of the memory references is empty, it doesn't matter what the
  // pointer values are.
  if (V1Size == 0 || V2Size == 0)
    return NoAlias;

  // Strip off any casts if they exist.
  V1 = V1->stripPointerCasts();
  V2 = V2->stripPointerCasts();

  // Are we checking for alias of the same value?
  if (V1 == V2 && Rel == Same) return MustAlias;

  if (!V1->getType()->isPointerTy() || !V2->getType()->isPointerTy())
    return NoAlias;  // Scalars cannot alias each other

  // Figure out what objects these things are pointing to if we can.
  const Value *O1 = GetUnderlyingObject(V1, currentMod->getDataLayout());
  const Value *O2 = GetUnderlyingObject(V2, currentMod->getDataLayout());

  // Null values in the default address space don't point to any object, so they
  // don't alias any other pointer.
  if (const ConstantPointerNull *CPN = dyn_cast<ConstantPointerNull>(O1))
    if (CPN->getType()->getAddressSpace() == 0)
      return NoAlias;
  if (const ConstantPointerNull *CPN = dyn_cast<ConstantPointerNull>(O2))
    if (CPN->getType()->getAddressSpace() == 0)
      return NoAlias;

  if (O1 != O2) {
    // If V1/V2 point to two different objects we know that we have no alias.
    if (isIdentifiedObject(O1) && isIdentifiedObject(O2))
      return NoAlias;

    // Constant pointers can't alias with non-const isIdentifiedObject objects.
    if ((isa<Constant>(O1) && isIdentifiedObject(O2) && !isa<Constant>(O2)) ||
        (isa<Constant>(O2) && isIdentifiedObject(O1) && !isa<Constant>(O1)))
      return NoAlias;

    // Arguments can't alias with local allocations or noalias calls
    // in the same function.
    if (((isa<Argument>(O1) && (isa<AllocaInst>(O2) || isNoAliasCall(O2))) ||
         (isa<Argument>(O2) && (isa<AllocaInst>(O1) || isNoAliasCall(O1)))))
      return NoAlias;

    // Most objects can't alias null.
    if ((isa<ConstantPointerNull>(O2) && isKnownNonNull(O1)) ||
        (isa<ConstantPointerNull>(O1) && isKnownNonNull(O2)))
      return NoAlias;

    // If one pointer is the result of a call/invoke or load and the other is a
    // non-escaping local object within the same function, then we know the
    // object couldn't escape to a point where the call could return it.
    //
    // Note that if the pointers are in different functions, there are a variety
    // of complications. A call with a nocapture argument may still temporary
    // store the nocapture argument's value in a temporary memory location if
    // that memory location doesn't escape. Or it may pass a nocapture value to
    // other functions as long as they don't capture it.
    if (isEscapeSource(O1) && isNonEscapingLocalObject(O2))
      return NoAlias;
    if (isEscapeSource(O2) && isNonEscapingLocalObject(O1))
      return NoAlias;
  }

  // If the size of one access is larger than the entire object on the other
  // side, then we know such behavior is undefined and can assume no alias.
  const DataLayout *TD = getDataLayout();
  const TargetLibraryInfo *TLI = getTargetLibraryInfo();
  if (TD)
    if ((V1Size != UnknownSize && isObjectSmallerThan(O2, V1Size, *TD, *TLI)) ||
        (V2Size != UnknownSize && isObjectSmallerThan(O1, V2Size, *TD, *TLI)))
      return NoAlias;

  // FIXME: This isn't aggressively handling alias(GEP, PHI) for example: if the
  // GEP can't simplify, we don't even look at the PHI cases.
  if (!isa<GEPOperator>(V1) && isa<GEPOperator>(V2)) {
    std::swap(V1, V2);
    std::swap(V1Size, V2Size);
    std::swap(O1, O2);
  }

  if (const GEPOperator *GV1 = dyn_cast<GEPOperator>(V1)) {
    Remedies tmpR;
    liberty::LoopAA::AliasResult res =
        aliasGEP(GV1, V1Size, Rel, V2, V2Size, L, O1, O2, tmpR);
    if (res != MayAlias) {
      for (auto remed : tmpR)
        R.insert(remed);
    }
    return res;
  }

  if (isa<PHINode>(V2) && !isa<PHINode>(V1)) {
    std::swap(V1, V2);
    std::swap(V1Size, V2Size);
  }
  if (const PHINode *PN = dyn_cast<PHINode>(V1)) {
    Remedies tmpR;
    liberty::LoopAA::AliasResult res =
        aliasPHI(PN, V1Size, Rel, V2, V2Size, L, tmpR);
    if (res != MayAlias) {
      for (auto remed : tmpR)
        R.insert(remed);
    }
    return res;
  }

  if (isa<SelectInst>(V2) && !isa<SelectInst>(V1)) {
    std::swap(V1, V2);
    std::swap(V1Size, V2Size);
  }
  if (const SelectInst *S1 = dyn_cast<SelectInst>(V1)) {
    Remedies tmpR;
    liberty::LoopAA::AliasResult res =
        aliasSelect(S1, V1Size, Rel, V2, V2Size, L, tmpR);
    if (res != MayAlias) {
      for (auto remed : tmpR)
        R.insert(remed);
    }
    return res;
  }

  return MayAlias;
}
