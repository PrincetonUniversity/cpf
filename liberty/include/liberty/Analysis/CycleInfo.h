//===- llvm/Analysis/CycleInfo.h - Natural Cycle Calculator -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the CycleInfo class that is used to identify natural loops
// and determine the loop depth of various nodes of the CFG.  A natural loop
// has exactly one entry-point, which is called the header. Note that natural
// loops may actually be several loops that share the same header node.
//
// This analysis calculates the nesting structure of loops in a function.  For
// each natural loop identified, this analysis identifies natural loops
// contained entirely within the loop and the basic blocks the make up the loop.
//
// It can calculate on the fly various bits of information, for example:
//
//  * whether there is a preheader for the loop
//  * the number of back edges to the header
//  * whether or not a particular block branches out of the loop
//  * the successor blocks of the loop
//  * the loop depth
//  * the trip count
//  * etc...
//
//===----------------------------------------------------------------------===//

#ifndef CYCLE_INFO_H
#define CYCLE_INFO_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <map>

namespace liberty {

  using namespace llvm;

  class Cycle : public LoopBase<BasicBlock, Cycle>
  {
    public:
      Cycle() {}
      /// isLoopInvariant - Return true if the specified value is loop invariant
      ///
      bool isLoopInvariant(Value *V) const;

      /// hasLoopInvariantOperands - Return true if all the operands of the
      /// specified instruction are loop invariant.
      bool hasLoopInvariantOperands(Instruction *I) const;

      /// makeLoopInvariant - If the given value is an instruction inside of the
      /// loop and it can be hoisted, do so to make it trivially loop-invariant.
      /// Return true if the value after any hoisting is loop invariant. This
      /// function can be used as a slightly more aggressive replacement for
      /// isLoopInvariant.
      ///
      /// If InsertPt is specified, it is the point to hoist instructions to.
      /// If null, the terminator of the loop preheader is used.
      ///
      bool makeLoopInvariant(Value *V, bool &Changed,
          Instruction *InsertPt = 0) const;

      /// makeLoopInvariant - If the given instruction is inside of the
      /// loop and it can be hoisted, do so to make it trivially loop-invariant.
      /// Return true if the instruction after any hoisting is loop invariant. This
      /// function can be used as a slightly more aggressive replacement for
      /// isLoopInvariant.
      ///
      /// If InsertPt is specified, it is the point to hoist instructions to.
      /// If null, the terminator of the loop preheader is used.
      ///
      bool makeLoopInvariant(Instruction *I, bool &Changed,
          Instruction *InsertPt = 0) const;

      /// getCanonicalInductionVariable - Check to see if the loop has a canonical
      /// induction variable: an integer recurrence that starts at 0 and increments
      /// by one each time through the loop.  If so, return the phi node that
      /// corresponds to it.
      ///
      /// The IndVarSimplify pass transforms loops to have a canonical induction
      /// variable.
      ///
      PHINode *getCanonicalInductionVariable() const;

      /// getTripCount - Return a loop-invariant LLVM value indicating the number of
      /// times the loop will be executed.  Note that this means that the backedge
      /// of the loop executes N-1 times.  If the trip-count cannot be determined,
      /// this returns null.
      ///
      /// The IndVarSimplify pass transforms loops to have a form that this
      /// function easily understands.
      ///
      Value *getTripCount() const;

      /// getSmallConstantTripCount - Returns the trip count of this loop as a
      /// normal unsigned value, if possible. Returns 0 if the trip count is unknown
      /// of not constant. Will also return 0 if the trip count is very large
      /// (>= 2^32)
      ///
      /// The IndVarSimplify pass transforms loops to have a form that this
      /// function easily understands.
      ///
      unsigned getSmallConstantTripCount() const;

      /// getSmallConstantTripMultiple - Returns the largest constant divisor of the
      /// trip count of this loop as a normal unsigned value, if possible. This
      /// means that the actual trip count is always a multiple of the returned
      /// value (don't forget the trip count could very well be zero as well!).
      ///
      /// Returns 1 if the trip count is unknown or not guaranteed to be the
      /// multiple of a constant (which is also the case if the trip count is simply
      /// constant, use getSmallConstantTripCount for that case), Will also return 1
      /// if the trip count is very large (>= 2^32).
      unsigned getSmallConstantTripMultiple() const;

      /// isLCSSAForm - Return true if the Loop is in LCSSA form
      bool isLCSSAForm(DominatorTree &DT) const;

      /// isLoopSimplifyForm - Return true if the Loop is in the form that
      /// the LoopSimplify form transforms loops to, which is sometimes called
      /// normal form.
      bool isLoopSimplifyForm() const;

      /// hasDedicatedExits - Return true if no exit block for the loop
      /// has a predecessor that is outside the loop.
      bool hasDedicatedExits() const;

      /// getUniqueExitBlocks - Return all unique successor blocks of this loop.
      /// These are the blocks _outside of the current loop_ which are branched to.
      /// This assumes that loop exits are in canonical form.
      ///
      void getUniqueExitBlocks(SmallVectorImpl<BasicBlock *> &ExitBlocks) const;

      /// getUniqueExitBlock - If getUniqueExitBlocks would return exactly one
      /// block, return that block. Otherwise return null.
      BasicBlock *getUniqueExitBlock() const;

      void dump() const;
    private:
      friend class LoopInfoBase<BasicBlock, Cycle>;
      explicit Cycle(BasicBlock *BB) : LoopBase<BasicBlock, Cycle>(BB) {}

  };

class CycleInfo : public FunctionPass {
  LoopInfoBase<BasicBlock, Cycle> LI;
  friend class LoopBase<BasicBlock, Cycle>;

  void operator=(const CycleInfo &); // do not implement
  CycleInfo(const CycleInfo &);       // do not implement
public:
  static char ID; // Pass identification, replacement for typeid

  CycleInfo() : FunctionPass(ID) {
    initializeLoopInfoPass(*PassRegistry::getPassRegistry());
  }

  LoopInfoBase<BasicBlock, Cycle>& getBase() { return LI; }

  /// iterator/begin/end - The interface to the top-level loops in the current
  /// function.
  ///
  typedef LoopInfoBase<BasicBlock, Cycle>::iterator iterator;
  inline iterator begin() const { return LI.begin(); }
  inline iterator end() const { return LI.end(); }
  bool empty() const { return LI.empty(); }

  /// getLoopFor - Return the inner most loop that BB lives in.  If a basic
  /// block is in no loop (for example the entry node), null is returned.
  ///
  inline Cycle *getLoopFor(const BasicBlock *BB) const {
    return LI.getLoopFor(BB);
  }

  /// operator[] - same as getLoopFor...
  ///
  inline const Cycle *operator[](const BasicBlock *BB) const {
    return LI.getLoopFor(BB);
  }

  /// getLoopDepth - Return the loop nesting level of the specified block.  A
  /// depth of 0 means the block is not inside any loop.
  ///
  inline unsigned getLoopDepth(const BasicBlock *BB) const {
    return LI.getLoopDepth(BB);
  }

  // isLoopHeader - True if the block is a loop header node
  inline bool isLoopHeader(BasicBlock *BB) const {
    return LI.isLoopHeader(BB);
  }

  /// runOnFunction - Calculate the natural loop information.
  ///
  virtual bool runOnFunction(Function &F);

  virtual void verifyAnalysis() const;

  virtual void releaseMemory() { LI.releaseMemory(); }

  virtual void print(raw_ostream &O, const Module* M = 0) const;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  /// removeLoop - This removes the specified top-level loop from this loop info
  /// object.  The loop is not deleted, as it will presumably be inserted into
  /// another loop.
  inline Cycle *removeLoop(iterator I) { return LI.removeLoop(I); }

  /// changeLoopFor - Change the top-level loop that contains BB to the
  /// specified loop.  This should be used by transformations that restructure
  /// the loop hierarchy tree.
  inline void changeLoopFor(BasicBlock *BB, Cycle *L) {
    LI.changeLoopFor(BB, L);
  }

  /// changeTopLevelLoop - Replace the specified loop in the top-level loops
  /// list with the indicated loop.
  inline void changeTopLevelLoop(Cycle *OldLoop, Cycle *NewLoop) {
    LI.changeTopLevelLoop(OldLoop, NewLoop);
  }

  /// addTopLevelLoop - This adds the specified loop to the collection of
  /// top-level loops.
  inline void addTopLevelLoop(Cycle *New) {
    LI.addTopLevelLoop(New);
  }

  /// removeBlock - This method completely removes BB from all data structures,
  /// including all of the Loop objects it is nested in and our mapping from
  /// BasicBlocks to loops.
  void removeBlock(BasicBlock *BB) {
    LI.removeBlock(BB);
  }

  /// replacementPreservesLCSSAForm - Returns true if replacing From with To
  /// everywhere is guaranteed to preserve LCSSA form.
  bool replacementPreservesLCSSAForm(Instruction *From, Value *To) {
    // Preserving LCSSA form is only problematic if the replacing value is an
    // instruction.
    Instruction *I = dyn_cast<Instruction>(To);
    if (!I) return true;
    // If both instructions are defined in the same basic block then replacement
    // cannot break LCSSA form.
    if (I->getParent() == From->getParent())
      return true;
    // If the instruction is not defined in a loop then it can safely replace
    // anything.
    Cycle *ToLoop = getLoopFor(I->getParent());
    if (!ToLoop) return true;
    // If the replacing instruction is defined in the same loop as the original
    // instruction, or in a loop that contains it as an inner loop, then using
    // it as a replacement will not break LCSSA form.
    return ToLoop->contains(getLoopFor(From->getParent()));
  }
};



} // End llvm namespace

#endif
