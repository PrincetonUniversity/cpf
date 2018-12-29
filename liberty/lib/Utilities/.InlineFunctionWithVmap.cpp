//===- InlineFunction.cpp - Code to perform function inlining -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements inlining of a function into a call site, resolving
// parameters and the return value as appropriate.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Transforms/Utils/Local.h"

#include "liberty/Utilities/InlineFunctionWithVmap.h"

namespace liberty
{
using namespace llvm;

namespace {
  /// A class for recording information about inlining through an invoke.
  class InvokeInliningInfo {
    BasicBlock *OuterResumeDest; ///< Destination of the invoke's unwind.
    BasicBlock *InnerResumeDest; ///< Destination for the callee's resume.
    LandingPadInst *CallerLPad;  ///< LandingPadInst associated with the invoke.
    PHINode *InnerEHValuesPHI;   ///< PHI for EH values from landingpad insts.
    SmallVector<Value*, 8> UnwindDestPHIValues;

  public:
    InvokeInliningInfo(InvokeInst *II)
      : OuterResumeDest(II->getUnwindDest()), InnerResumeDest(0),
        CallerLPad(0), InnerEHValuesPHI(0) {
      // If there are PHI nodes in the unwind destination block, we need to keep
      // track of which values came into them from the invoke before removing
      // the edge from this block.
      BasicBlock *InvokeBB = II->getParent();
      BasicBlock::iterator I = OuterResumeDest->begin();
      for (; isa<PHINode>(I); ++I) {
        // Save the value to use for this edge.
        PHINode *PHI = cast<PHINode>(I);
        UnwindDestPHIValues.push_back(PHI->getIncomingValueForBlock(InvokeBB));
      }

      CallerLPad = cast<LandingPadInst>(I);
    }

    /// getOuterResumeDest - The outer unwind destination is the target of
    /// unwind edges introduced for calls within the inlined function.
    BasicBlock *getOuterResumeDest() const {
      return OuterResumeDest;
    }

    BasicBlock *getInnerResumeDest();

    LandingPadInst *getLandingPadInst() const { return CallerLPad; }

    /// forwardResume - Forward the 'resume' instruction to the caller's landing
    /// pad block. When the landing pad block has only one predecessor, this is
    /// a simple branch. When there is more than one predecessor, we need to
    /// split the landing pad block after the landingpad instruction and jump
    /// to there.
    void forwardResume(ResumeInst *RI);

    /// addIncomingPHIValuesFor - Add incoming-PHI values to the unwind
    /// destination block for the given basic block, using the values for the
    /// original invoke's source block.
    void addIncomingPHIValuesFor(BasicBlock *BB) const {
      addIncomingPHIValuesForInto(BB, OuterResumeDest);
    }

    void addIncomingPHIValuesForInto(BasicBlock *src, BasicBlock *dest) const {
      BasicBlock::iterator I = dest->begin();
      for (unsigned i = 0, e = UnwindDestPHIValues.size(); i != e; ++i, ++I) {
        PHINode *phi = cast<PHINode>(I);
        phi->addIncoming(UnwindDestPHIValues[i], src);
      }
    }
  };
}

// Clone OldFunc into NewFunc, transforming the old arguments into references to
// VMap values.
//
void CloneFunctionIntoWithVMap(Function *NewFunc, const Function *OldFunc,
                             ValueToValueMapTy &VMap,
                             bool ModuleLevelChanges,
                             SmallVectorImpl<ReturnInst*> &Returns,
                             StringRef NameSuffix, ClonedCodeInfo *CodeInfo=0,
                             ValueMapTypeRemapper *TypeMapper=0) {
  assert(NameSuffix && "NameSuffix cannot be null!");

#ifndef NDEBUG
  for (Function::const_arg_iterator I = OldFunc->arg_begin(),
       E = OldFunc->arg_end(); I != E; ++I)
    assert(VMap.count(I) && "No mapping from source argument specified!");
#endif

/* DON'T clone any attributes
  // Clone any attributes.
  if (NewFunc->arg_size() == OldFunc->arg_size())
    NewFunc->copyAttributesFrom(OldFunc);
  else {
    //Some arguments were deleted with the VMap. Copy arguments one by one
    for (Function::const_arg_iterator I = OldFunc->arg_begin(),
           E = OldFunc->arg_end(); I != E; ++I)
      if (Argument* Anew = dyn_cast<Argument>(VMap[I]))
        Anew->addAttr( OldFunc->getAttributes()
                       .getParamAttributes(I->getArgNo() + 1));
    NewFunc->setAttributes(NewFunc->getAttributes()
                           .addAttr(0, OldFunc->getAttributes()
                                     .getRetAttributes()));
    NewFunc->setAttributes(NewFunc->getAttributes()
                           .addAttr(~0, OldFunc->getAttributes()
                                     .getFnAttributes()));

  }
*/

  // Loop over all of the basic blocks in the function, cloning them as
  // appropriate.  Note that we save BE this way in order to handle cloning of
  // recursive functions into themselves.
  //
  for (Function::const_iterator BI = OldFunc->begin(), BE = OldFunc->end();
       BI != BE; ++BI) {
    const BasicBlock &BB = *BI;

    // Create a new basic block and copy instructions into it!
    BasicBlock *CBB = CloneBasicBlock(&BB, VMap, NameSuffix, NewFunc, CodeInfo);

    // Add basic block mapping.
    VMap[&BB] = CBB;

    // It is only legal to clone a function if a block address within that
    // function is never referenced outside of the function.  Given that, we
    // want to map block addresses from the old function to block addresses in
    // the clone. (This is different from the generic ValueMapper
    // implementation, which generates an invalid blockaddress when
    // cloning a function.)
    if (BB.hasAddressTaken()) {
      Constant *OldBBAddr = BlockAddress::get(const_cast<Function*>(OldFunc),
                                              const_cast<BasicBlock*>(&BB));
      VMap[OldBBAddr] = BlockAddress::get(NewFunc, CBB);
    }

    // Note return instructions for the caller.
    if (ReturnInst *RI = dyn_cast<ReturnInst>(CBB->getTerminator()))
      Returns.push_back(RI);
  }

  // Loop over all of the instructions in the function, fixing up operand
  // references as we go.  This uses VMap to do all the hard work.
  for (Function::iterator BB = cast<BasicBlock>(VMap[OldFunc->begin()]),
         BE = NewFunc->end(); BB != BE; ++BB)
    // Loop over all instructions, fixing each one as we find it...
    for (BasicBlock::iterator II = BB->begin(); II != BB->end(); ++II)
      RemapInstruction(II, VMap,
                       ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges,
                       TypeMapper);
}


/// getInnerResumeDest - Get or create a target for the branch from ResumeInsts.
BasicBlock *InvokeInliningInfo::getInnerResumeDest() {
  if (InnerResumeDest) return InnerResumeDest;

  // Split the landing pad.
  BasicBlock::iterator SplitPoint = CallerLPad; ++SplitPoint;
  InnerResumeDest =
    OuterResumeDest->splitBasicBlock(SplitPoint,
                                     OuterResumeDest->getName() + ".body");

  // The number of incoming edges we expect to the inner landing pad.
  const unsigned PHICapacity = 2;

  // Create corresponding new PHIs for all the PHIs in the outer landing pad.
  BasicBlock::iterator InsertPoint = InnerResumeDest->begin();
  BasicBlock::iterator I = OuterResumeDest->begin();
  for (unsigned i = 0, e = UnwindDestPHIValues.size(); i != e; ++i, ++I) {
    PHINode *OuterPHI = cast<PHINode>(I);
    PHINode *InnerPHI = PHINode::Create(OuterPHI->getType(), PHICapacity,
                                        OuterPHI->getName() + ".lpad-body",
                                        InsertPoint);
    OuterPHI->replaceAllUsesWith(InnerPHI);
    InnerPHI->addIncoming(OuterPHI, OuterResumeDest);
  }

  // Create a PHI for the exception values.
  InnerEHValuesPHI = PHINode::Create(CallerLPad->getType(), PHICapacity,
                                     "eh.lpad-body", InsertPoint);
  CallerLPad->replaceAllUsesWith(InnerEHValuesPHI);
  InnerEHValuesPHI->addIncoming(CallerLPad, OuterResumeDest);

  // All done.
  return InnerResumeDest;
}

/// forwardResume - Forward the 'resume' instruction to the caller's landing pad
/// block. When the landing pad block has only one predecessor, this is a simple
/// branch. When there is more than one predecessor, we need to split the
/// landing pad block after the landingpad instruction and jump to there.
void InvokeInliningInfo::forwardResume(ResumeInst *RI) {
  BasicBlock *Dest = getInnerResumeDest();
  BasicBlock *Src = RI->getParent();

  BranchInst::Create(Dest, Src);

  // Update the PHIs in the destination. They were inserted in an order which
  // makes this work.
  addIncomingPHIValuesForInto(Src, Dest);

  InnerEHValuesPHI->addIncoming(RI->getOperand(0), Src);
  RI->eraseFromParent();
}

/// HandleCallsInBlockInlinedThroughInvoke - When we inline a basic block into
/// an invoke, we have to turn all of the calls that can throw into
/// invokes.  This function analyze BB to see if there are any calls, and if so,
/// it rewrites them to be invokes that jump to InvokeDest and fills in the PHI
/// nodes in that block with the values specified in InvokeDestPHIValues.
///
/// Returns true to indicate that the next block should be skipped.
static bool HandleCallsInBlockInlinedThroughInvoke(BasicBlock *BB,
                                                   InvokeInliningInfo &Invoke,
                                                   CallsPromotedToInvoke &call2invoke) {
  LandingPadInst *LPI = Invoke.getLandingPadInst();

  for (BasicBlock::iterator BBI = BB->begin(), E = BB->end(); BBI != E; ) {
    Instruction *I = BBI++;

    if (LandingPadInst *L = dyn_cast<LandingPadInst>(I)) {
      unsigned NumClauses = LPI->getNumClauses();
      L->reserveClauses(NumClauses);
      for (unsigned i = 0; i != NumClauses; ++i)
        L->addClause(LPI->getClause(i));
    }

    // We only need to check for function calls: inlined invoke
    // instructions require no special handling.
    CallInst *CI = dyn_cast<CallInst>(I);

    // If this call cannot unwind, don't convert it to an invoke.
    if (!CI || CI->doesNotThrow())
      continue;

    // Convert this function call into an invoke instruction.  First, split the
    // basic block.
    BasicBlock *Split = BB->splitBasicBlock(CI, CI->getName()+".noexc");

    // Delete the unconditional branch inserted by splitBasicBlock
    BB->getInstList().pop_back();


    // Create the new invoke instruction.
    ImmutableCallSite CS(CI);
    SmallVector<Value*, 8> InvokeArgs(CS.arg_begin(), CS.arg_end());
    InvokeInst *II = InvokeInst::Create(CI->getCalledValue(), Split,
                                        Invoke.getOuterResumeDest(),
                                        InvokeArgs, CI->getName(), BB);
    II->setCallingConv(CI->getCallingConv());
    II->setAttributes(CI->getAttributes());

    // Record this transformation
    call2invoke.push_back(II);

    // Make sure that anything using the call now uses the invoke!  This also
    // updates the CallGraph if present, because it uses a WeakVH.
    CI->replaceAllUsesWith(II);

    // Delete the original call
    Split->getInstList().pop_front();

    // Update any PHI nodes in the exceptional block to indicate that there is
    // now a new entry in them.
    Invoke.addIncomingPHIValuesFor(BB);
    return false;
  }

  return false;
}

/// HandleInlinedInvoke - If we inlined an invoke site, we need to convert calls
/// in the body of the inlined function into invokes.
///
/// II is the invoke instruction being inlined.  FirstNewBlock is the first
/// block of the inlined code (the last block is the end of the function),
/// and InlineCodeInfo is information about the code that got inlined.
static void HandleInlinedInvoke(InvokeInst *II, BasicBlock *FirstNewBlock,
                                ClonedCodeInfo &InlinedCodeInfo,
                                CallsPromotedToInvoke &call2invoke) {
  BasicBlock *InvokeDest = II->getUnwindDest();

  Function *Caller = FirstNewBlock->getParent();

  // The inlined code is currently at the end of the function, scan from the
  // start of the inlined code to its end, checking for stuff we need to
  // rewrite.  If the code doesn't have calls or unwinds, we know there is
  // nothing to rewrite.
  if (!InlinedCodeInfo.ContainsCalls) {
    // Now that everything is happy, we have one final detail.  The PHI nodes in
    // the exception destination block still have entries due to the original
    // invoke instruction.  Eliminate these entries (which might even delete the
    // PHI node) now.
    InvokeDest->removePredecessor(II->getParent());
    return;
  }

  InvokeInliningInfo Invoke(II);

  for (Function::iterator BB = FirstNewBlock, E = Caller->end(); BB != E; ++BB){
    if (InlinedCodeInfo.ContainsCalls)
      if (HandleCallsInBlockInlinedThroughInvoke(BB, Invoke, call2invoke)) {
        // Honor a request to skip the next block.
        ++BB;
        continue;
      }

    if (ResumeInst *RI = dyn_cast<ResumeInst>(BB->getTerminator()))
      Invoke.forwardResume(RI);
  }

  // Now that everything is happy, we have one final detail.  The PHI nodes in
  // the exception destination block still have entries due to the original
  // invoke instruction.  Eliminate these entries (which might even delete the
  // PHI node) now.
  InvokeDest->removePredecessor(II->getParent());
}

/// UpdateCallGraphAfterInlining - Once we have cloned code over from a callee
/// into the caller, update the specified callgraph to reflect the changes we
/// made.  Note that it's possible that not all code was copied over, so only
/// some edges of the callgraph may remain.
static void UpdateCallGraphAfterInlining(CallSite CS,
                                         Function::iterator FirstNewBlock,
                                         ValueToValueMapTy &VMap,
                                         InlineFunctionInfo &IFI) {
  CallGraph &CG = *IFI.CG;
  const Function *Caller = CS.getInstruction()->getParent()->getParent();
  const Function *Callee = CS.getCalledFunction();
  CallGraphNode *CalleeNode = CG[Callee];
  CallGraphNode *CallerNode = CG[Caller];

  // Since we inlined some uninlined call sites in the callee into the caller,
  // add edges from the caller to all of the callees of the callee.
  CallGraphNode::iterator I = CalleeNode->begin(), E = CalleeNode->end();

  // Consider the case where CalleeNode == CallerNode.
  CallGraphNode::CalledFunctionsVector CallCache;
  if (CalleeNode == CallerNode) {
    CallCache.assign(I, E);
    I = CallCache.begin();
    E = CallCache.end();
  }

  for (; I != E; ++I) {
    const Value *OrigCall = I->first;

    ValueToValueMapTy::iterator VMI = VMap.find(OrigCall);
    // Only copy the edge if the call was inlined!
    if (VMI == VMap.end() || VMI->second == 0)
      continue;

    // If the call was inlined, but then constant folded, there is no edge to
    // add.  Check for this case.
    Instruction *NewCall = dyn_cast<Instruction>(VMI->second);
    if (NewCall == 0) continue;

    // Remember that this call site got inlined for the client of
    // InlineFunction.
    IFI.InlinedCalls.push_back(NewCall);

    // It's possible that inlining the callsite will cause it to go from an
    // indirect to a direct call by resolving a function pointer.  If this
    // happens, set the callee of the new call site to a more precise
    // destination.  This can also happen if the call graph node of the caller
    // was just unnecessarily imprecise.
    if (I->second->getFunction() == 0)
      if (Function *F = CallSite(NewCall).getCalledFunction()) {
        // Indirect call site resolved to direct call.
        CallerNode->addCalledFunction(CallSite(NewCall), CG[F]);

        continue;
      }

    CallerNode->addCalledFunction(CallSite(NewCall), I->second);
  }

  // Update the call graph by deleting the edge from Callee to Caller.  We must
  // do this after the loop above in case Caller and Callee are the same.
  CallerNode->removeCallEdgeFor(CS);
}

/// HandleByValArgument - When inlining a call site that has a byval argument,
/// we have to make the implicit memcpy explicit by adding it.
static Value *HandleByValArgument(Value *Arg, Instruction *TheCall,
                                  const Function *CalledFunc,
                                  InlineFunctionInfo &IFI,
                                  unsigned ByValAlignment) {
  Type *AggTy = cast<PointerType>(Arg->getType())->getElementType();

  // If the called function is readonly, then it could not mutate the caller's
  // copy of the byval'd memory.  In this case, it is safe to elide the copy and
  // temporary.
  if (CalledFunc->onlyReadsMemory()) {
    // If the byval argument has a specified alignment that is greater than the
    // passed in pointer, then we either have to round up the input pointer or
    // give up on this transformation.
    if (ByValAlignment <= 1)  // 0 = unspecified, 1 = no particular alignment.
      return Arg;

    // If the pointer is already known to be sufficiently aligned, or if we can
    // round it up to a larger alignment, then we don't need a temporary.
    if (getOrEnforceKnownAlignment(Arg, ByValAlignment,
                                   IFI.TD) >= ByValAlignment)
      return Arg;

    // Otherwise, we have to make a memcpy to get a safe alignment.  This is bad
    // for code quality, but rarely happens and is required for correctness.
  }

  LLVMContext &Context = Arg->getContext();

  Type *VoidPtrTy = Type::getInt8PtrTy(Context);

  // Create the alloca.  If we have DataLayout, use nice alignment.
  unsigned Align = 1;
  if (IFI.TD)
    Align = IFI.TD->getPrefTypeAlignment(AggTy);

  // If the byval had an alignment specified, we *must* use at least that
  // alignment, as it is required by the byval argument (and uses of the
  // pointer inside the callee).
  Align = std::max(Align, ByValAlignment);

  Function *Caller = TheCall->getParent()->getParent();

  Value *NewAlloca = new AllocaInst(AggTy, 0, Align, Arg->getName(),
                                    &*Caller->begin()->begin());
  // Emit a memcpy.
  Type *Tys[3] = {VoidPtrTy, VoidPtrTy, Type::getInt64Ty(Context)};
  Function *MemCpyFn = Intrinsic::getDeclaration(Caller->getParent(),
                                                 Intrinsic::memcpy,
                                                 Tys);
  Value *DestCast = new BitCastInst(NewAlloca, VoidPtrTy, "tmp", TheCall);
  Value *SrcCast = new BitCastInst(Arg, VoidPtrTy, "tmp", TheCall);

  Value *Size;
  if (IFI.TD == 0)
    Size = ConstantExpr::getSizeOf(AggTy);
  else
    Size = ConstantInt::get(Type::getInt64Ty(Context),
                            IFI.TD->getTypeStoreSize(AggTy));

  // Always generate a memcpy of alignment 1 here because we don't know
  // the alignment of the src pointer.  Other optimizations can infer
  // better alignment.
  Value *CallArgs[] = {
    DestCast, SrcCast, Size,
    ConstantInt::get(Type::getInt32Ty(Context), 1),
    ConstantInt::getFalse(Context) // isVolatile
  };
  IRBuilder<>(TheCall).CreateCall(MemCpyFn, CallArgs);

  // Uses of the argument in the function should use our new alloca
  // instead.
  return NewAlloca;
}

// isUsedByLifetimeMarker - Check whether this Value is used by a lifetime
// intrinsic.
static bool isUsedByLifetimeMarker(Value *V) {
  for (Value::user_iterator UI = V->user_begin(), UE = V->user_end(); UI != UE;
       ++UI) {
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(*UI)) {
      switch (II->getIntrinsicID()) {
      default: break;
      case Intrinsic::lifetime_start:
      case Intrinsic::lifetime_end:
        return true;
      }
    }
  }
  return false;
}

// hasLifetimeMarkers - Check whether the given alloca already has
// lifetime.start or lifetime.end intrinsics.
static bool hasLifetimeMarkers(AllocaInst *AI) {
  Type *Int8PtrTy = Type::getInt8PtrTy(AI->getType()->getContext());
  if (AI->getType() == Int8PtrTy)
    return isUsedByLifetimeMarker(AI);

  // Do a scan to find all the casts to i8*.
  for (Value::user_iterator I = AI->user_begin(), E = AI->user_end(); I != E;
       ++I) {
    if (I->getType() != Int8PtrTy) continue;
    if (I->stripPointerCasts() != AI) continue;
    if (isUsedByLifetimeMarker(*I))
      return true;
  }
  return false;
}

/// updateInlinedAtInfo - Helper function used by fixupLineNumbers to
/// recursively update InlinedAtEntry of a DebugLoc.
static DebugLoc updateInlinedAtInfo(const DebugLoc &DL,
                                    const DebugLoc &InlinedAtDL,
                                    LLVMContext &Ctx) {
  if (MDNode *IA = DL.getInlinedAt(Ctx)) {
    DebugLoc NewInlinedAtDL
      = updateInlinedAtInfo(DebugLoc::getFromDILocation(IA), InlinedAtDL, Ctx);
    return DebugLoc::get(DL.getLine(), DL.getCol(), DL.getScope(Ctx),
                         NewInlinedAtDL.getAsMDNode(Ctx));
  }

  return DebugLoc::get(DL.getLine(), DL.getCol(), DL.getScope(Ctx),
                       InlinedAtDL.getAsMDNode(Ctx));
}

/// fixupLineNumbers - Update inlined instructions' line numbers to
/// to encode location where these instructions are inlined.
static void fixupLineNumbers(Function *Fn, Function::iterator FI,
                             Instruction *TheCall) {
  DebugLoc TheCallDL = TheCall->getDebugLoc();
  if (TheCallDL.isUnknown())
    return;

  for (; FI != Fn->end(); ++FI) {
    for (BasicBlock::iterator BI = FI->begin(), BE = FI->end();
         BI != BE; ++BI) {
      DebugLoc DL = BI->getDebugLoc();
      if (!DL.isUnknown()) {
        BI->setDebugLoc(updateInlinedAtInfo(DL, TheCallDL, BI->getContext()));
        if (DbgValueInst *DVI = dyn_cast<DbgValueInst>(BI)) {
          LLVMContext &Ctx = BI->getContext();
          MDNode *InlinedAt = BI->getDebugLoc().getInlinedAt(Ctx);
          DVI->setOperand(2, createInlinedVariable(DVI->getVariable(),
                                                   InlinedAt, Ctx));
        }
      }
    }
  }
}

/// InlineFunction - This function inlines the called function into the basic
/// block of the caller.  This returns false if it is not possible to inline
/// this call.  The program is still in a well defined state if this occurs
/// though.
///
/// Note that this only does one level of inlining.  For example, if the
/// instruction 'call B' is inlined, and 'B' calls 'C', then the call to 'C' now
/// exists in the instruction stream.  Similarly this will inline a recursive
/// function by one level.
bool InlineFunctionWithVmap(CallSite CS, InlineFunctionInfo &IFI, ValueToValueMapTy &VMap, CallsPromotedToInvoke &call2invoke, bool InsertLifetime) {
  Instruction *TheCall = CS.getInstruction();
  assert(TheCall->getParent() && TheCall->getParent()->getParent() &&
         "Instruction not in function!");

  // If IFI has any state in it, zap it before we fill it in.
  IFI.reset();

  const Function *CalledFunc = CS.getCalledFunction();
  if (CalledFunc == 0 ||          // Can't inline external function or indirect
      CalledFunc->isDeclaration() || // call, or call to a vararg function!
      CalledFunc->getFunctionType()->isVarArg()) return false;

  // If the call to the callee is not a tail call, we must clear the 'tail'
  // flags on any calls that we inline.
  bool MustClearTailCallFlags =
    !(isa<CallInst>(TheCall) && cast<CallInst>(TheCall)->isTailCall());

  // If the call to the callee cannot throw, set the 'nounwind' flag on any
  // calls that we inline.
  bool MarkNoUnwind = CS.doesNotThrow();

  BasicBlock *OrigBB = TheCall->getParent();
  Function *Caller = OrigBB->getParent();

  // GC poses two hazards to inlining, which only occur when the callee has GC:
  //  1. If the caller has no GC, then the callee's GC must be propagated to the
  //     caller.
  //  2. If the caller has a differing GC, it is invalid to inline.
  if (CalledFunc->hasGC()) {
    if (!Caller->hasGC())
      Caller->setGC(CalledFunc->getGC());
    else if (CalledFunc->getGC() != Caller->getGC())
      return false;
  }

  // Get the personality function from the callee if it contains a landing pad.
  Value *CalleePersonality = 0;
  for (Function::const_iterator I = CalledFunc->begin(), E = CalledFunc->end();
       I != E; ++I)
    if (const InvokeInst *II = dyn_cast<InvokeInst>(I->getTerminator())) {
      const BasicBlock *BB = II->getUnwindDest();
      const LandingPadInst *LP = BB->getLandingPadInst();
      CalleePersonality = LP->getPersonalityFn();
      break;
    }

  // Find the personality function used by the landing pads of the caller. If it
  // exists, then check to see that it matches the personality function used in
  // the callee.
  if (CalleePersonality) {
    for (Function::const_iterator I = Caller->begin(), E = Caller->end();
         I != E; ++I)
      if (const InvokeInst *II = dyn_cast<InvokeInst>(I->getTerminator())) {
        const BasicBlock *BB = II->getUnwindDest();
        const LandingPadInst *LP = BB->getLandingPadInst();

        // If the personality functions match, then we can perform the
        // inlining. Otherwise, we can't inline.
        // TODO: This isn't 100% true. Some personality functions are proper
        //       supersets of others and can be used in place of the other.
        if (LP->getPersonalityFn() != CalleePersonality)
          return false;

        break;
      }
  }

  // Get an iterator to the last basic block in the function, which will have
  // the new function inlined after it.
  Function::iterator LastBlock = &Caller->back();

  // Make sure to capture all of the return instructions from the cloned
  // function.
  SmallVector<ReturnInst*, 8> Returns;
  ClonedCodeInfo InlinedFunctionInfo;
  Function::iterator FirstNewBlock;

  assert(CalledFunc->arg_size() == CS.arg_size() &&
         "No varargs calls can be inlined!");

  // Calculate the vector of arguments to pass into the function cloner, which
  // matches up the formal to the actual argument values.
  CallSite::arg_iterator AI = CS.arg_begin();
  unsigned ArgNo = 0;
  for (Function::const_arg_iterator I = CalledFunc->arg_begin(),
       E = CalledFunc->arg_end(); I != E; ++I, ++AI, ++ArgNo) {
    Value *ActualArg = *AI;

    // When byval arguments actually inlined, we need to make the copy implied
    // by them explicit.  However, we don't do this if the callee is readonly
    // or readnone, because the copy would be unneeded: the callee doesn't
    // modify the struct.
    if (CS.isByValArgument(ArgNo)) {
      ActualArg = HandleByValArgument(ActualArg, TheCall, CalledFunc, IFI,
                                      CalledFunc->getParamAlignment(ArgNo+1));

      // Calls that we inline may use the new alloca, so we need to clear
      // their 'tail' flags if HandleByValArgument introduced a new alloca and
      // the callee has calls.
      MustClearTailCallFlags |= ActualArg != *AI;
    }

    VMap[I] = ActualArg;
  }

  // We want the inliner to prune the code as it copies.  We would LOVE to
  // have no dead or constant instructions leftover after inlining occurs
  // (which can happen, e.g., because an argument was constant), but we'll be
  // happy with whatever the cloner can do.
  //CloneAndPruneFunctionInto
  CloneFunctionIntoWithVMap(Caller, CalledFunc, VMap,
                            /*ModuleLevelChanges=*/false, Returns, ".inlvmap",
                            &InlinedFunctionInfo
                            /*, IFI.TD, TheCall*/);

  // Remember the first block that is newly cloned over.
  FirstNewBlock = LastBlock; ++FirstNewBlock;

  // Update the callgraph if requested.
  if (IFI.CG)
    UpdateCallGraphAfterInlining(CS, FirstNewBlock, VMap, IFI);

  // Update inlined instructions' line number information.
  fixupLineNumbers(Caller, FirstNewBlock, TheCall);

  // If there are any alloca instructions in the block that used to be the entry
  // block for the callee, move them to the entry block of the caller.  First
  // calculate which instruction they should be inserted before.  We insert the
  // instructions at the end of the current alloca list.
  {
    BasicBlock::iterator InsertPoint = Caller->begin()->begin();
    for (BasicBlock::iterator I = FirstNewBlock->begin(),
         E = FirstNewBlock->end(); I != E; ) {
      AllocaInst *AI = dyn_cast<AllocaInst>(I++);
      if (AI == 0) continue;

      // If the alloca is now dead, remove it.  This often occurs due to code
      // specialization.
      if (AI->use_empty()) {
        // TODO
        AI->eraseFromParent();
        continue;
      }

      if (!isa<Constant>(AI->getArraySize()))
        continue;

      // Keep track of the static allocas that we inline into the caller.
      IFI.StaticAllocas.push_back(AI);

      // Scan for the block of allocas that we can move over, and move them
      // all at once.
      while (isa<AllocaInst>(I) &&
             isa<Constant>(cast<AllocaInst>(I)->getArraySize())) {
        IFI.StaticAllocas.push_back(cast<AllocaInst>(I));
        ++I;
      }

      // Transfer all of the allocas over in a block.  Using splice means
      // that the instructions aren't removed from the symbol table, then
      // reinserted.
      Caller->getEntryBlock().getInstList().splice(InsertPoint,
                                                   FirstNewBlock->getInstList(),
                                                   AI, I);
    }
  }

  // Leave lifetime markers for the static alloca's, scoping them to the
  // function we just inlined.
  if (InsertLifetime && !IFI.StaticAllocas.empty()) {
    IRBuilder<> builder(FirstNewBlock->begin());
    for (unsigned ai = 0, ae = IFI.StaticAllocas.size(); ai != ae; ++ai) {
      AllocaInst *AI = IFI.StaticAllocas[ai];

      // If the alloca is already scoped to something smaller than the whole
      // function then there's no need to add redundant, less accurate markers.
      if (hasLifetimeMarkers(AI))
        continue;

      builder.CreateLifetimeStart(AI);
      for (unsigned ri = 0, re = Returns.size(); ri != re; ++ri) {
        IRBuilder<> builder(Returns[ri]);
        builder.CreateLifetimeEnd(AI);
      }
    }
  }

  // If the inlined code contained dynamic alloca instructions, wrap the inlined
  // code with llvm.stacksave/llvm.stackrestore intrinsics.
  if (InlinedFunctionInfo.ContainsDynamicAllocas) {
    Module *M = Caller->getParent();
    // Get the two intrinsics we care about.
    Function *StackSave = Intrinsic::getDeclaration(M, Intrinsic::stacksave);
    Function *StackRestore=Intrinsic::getDeclaration(M,Intrinsic::stackrestore);

    // Insert the llvm.stacksave.
    CallInst *SavedPtr = IRBuilder<>(FirstNewBlock, FirstNewBlock->begin())
      .CreateCall(StackSave, "savedstack");

    // Insert a call to llvm.stackrestore before any return instructions in the
    // inlined function.
    for (unsigned i = 0, e = Returns.size(); i != e; ++i) {
      IRBuilder<>(Returns[i]).CreateCall(StackRestore, SavedPtr);
    }
  }

  // If we are inlining tail call instruction through a call site that isn't
  // marked 'tail', we must remove the tail marker for any calls in the inlined
  // code.  Also, calls inlined through a 'nounwind' call site should be marked
  // 'nounwind'.
  if (InlinedFunctionInfo.ContainsCalls &&
      (MustClearTailCallFlags || MarkNoUnwind)) {
    for (Function::iterator BB = FirstNewBlock, E = Caller->end();
         BB != E; ++BB)
      for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I)
        if (CallInst *CI = dyn_cast<CallInst>(I)) {
          if (MustClearTailCallFlags)
            CI->setTailCall(false);
          if (MarkNoUnwind)
            CI->setDoesNotThrow();
        }
  }

  // If we are inlining for an invoke instruction, we must make sure to rewrite
  // any call instructions into invoke instructions.
  if (InvokeInst *II = dyn_cast<InvokeInst>(TheCall))
    // TODO
    HandleInlinedInvoke(II, FirstNewBlock, InlinedFunctionInfo, call2invoke);

  // Otherwise, we have the normal case, of more than one block to inline or
  // multiple return sites.

  // We want to clone the entire callee function into the hole between the
  // "starter" and "ender" blocks.  How we accomplish this depends on whether
  // this is an invoke instruction or a call instruction.
  BasicBlock *AfterCallBB;
  if (InvokeInst *II = dyn_cast<InvokeInst>(TheCall)) {

    // Add an unconditional branch to make this look like the CallInst case...
    BranchInst *NewBr = BranchInst::Create(II->getNormalDest(), TheCall);

    // Split the basic block.  This guarantees that no PHI nodes will have to be
    // updated due to new incoming edges, and make the invoke case more
    // symmetric to the call case.
    AfterCallBB = OrigBB->splitBasicBlock(NewBr,
                                          CalledFunc->getName()+".exit");

  } else {  // It's a call
    // If this is a call instruction, we need to split the basic block that
    // the call lives in.
    //
    AfterCallBB = OrigBB->splitBasicBlock(TheCall,
                                          CalledFunc->getName()+".exit");
  }

  // Change the branch that used to go to AfterCallBB to branch to the first
  // basic block of the inlined function.
  //
  TerminatorInst *Br = OrigBB->getTerminator();
  assert(Br && Br->getOpcode() == Instruction::Br &&
         "splitBasicBlock broken!");
  Br->setOperand(0, FirstNewBlock);


  // Now that the function is correct, make it a little bit nicer.  In
  // particular, move the basic blocks inserted from the end of the function
  // into the space made by splitting the source basic block.
  Caller->getBasicBlockList().splice(AfterCallBB, Caller->getBasicBlockList(),
                                     FirstNewBlock, Caller->end());

  // Handle all of the return instructions that we just cloned in, and eliminate
  // any users of the original call/invoke instruction.
  Type *RTy = CalledFunc->getReturnType();

  PHINode *PHI = 0;
  if (Returns.size() > 0) {
    // The PHI node should go at the front of the new basic block to merge all
    // possible incoming values.
    if (!TheCall->use_empty()) {
      PHI = PHINode::Create(RTy, Returns.size(), TheCall->getName(),
                            AfterCallBB->begin());
      // Anything that used the result of the function call should now use the
      // PHI node as their operand.
      TheCall->replaceAllUsesWith(PHI);
    }

    // Loop over all of the return instructions adding entries to the PHI node
    // as appropriate.
    if (PHI) {
      for (unsigned i = 0, e = Returns.size(); i != e; ++i) {
        ReturnInst *RI = Returns[i];
        assert(RI->getReturnValue()->getType() == PHI->getType() &&
               "Ret value not consistent in function!");
        PHI->addIncoming(RI->getReturnValue(), RI->getParent());
      }
    }


    // Add a branch to the merge points and remove return instructions.
    for (unsigned i = 0, e = Returns.size(); i != e; ++i) {
      ReturnInst *RI = Returns[i];
      BranchInst::Create(AfterCallBB, RI);
      // TODO
      RI->eraseFromParent();
    }
  }
  else if (!TheCall->use_empty()) {
    // No returns, but something is using the return value of the call.  Just
    // nuke the result.
    TheCall->replaceAllUsesWith(UndefValue::get(TheCall->getType()));
  }

  // Since we are now done with the Call/Invoke, we can delete it.
  TheCall->eraseFromParent();

  // We should always be able to fold the entry block of the function into the
  // single predecessor of the block...
  assert(cast<BranchInst>(Br)->isUnconditional() && "splitBasicBlock broken!");
  BasicBlock *CalleeEntry = cast<BranchInst>(Br)->getSuccessor(0);

  // If we inserted a phi node, check to see if it has a single value (e.g. all
  // the entries are the same or undef).  If so, remove the PHI so it doesn't
  // block other optimizations.
  if (PHI) {
    if (Value *V = SimplifyInstruction(PHI, IFI.TD)) {
      PHI->replaceAllUsesWith(V);
      // TODO
      PHI->eraseFromParent();
    }
  }

  return true;
}
}
