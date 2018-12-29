#ifndef LLVM_LIBERTY_LIVE_VALUES_H
#define LLVM_LIBERTY_LIVE_VALUES_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/BitVector.h"

#include <vector>

namespace liberty
{
  using namespace llvm;

  struct LiveValues
  {
    typedef DenseMap<const Value*,unsigned> Value2Num;
    typedef std::vector<const Value*> Num2Value;
    typedef DenseMap<const BasicBlock*, BitVector> ValueSets;

    typedef std::vector<const Value*> ValueList;

    /// Perform dataflow analysis on
    /// fcn to compute live values at
    /// the beginning and end of every
    /// basic block.  Optionally, exclude
    /// function arguments from this
    /// analysis.
    LiveValues(const Function &fcn, bool includeFcnArgs = true);

    /// Compute a vector of Value pointers
    /// representing values which are
    /// live-in to basic block bb.
    /// This will conservatively assume that
    /// ALL phi operands are used.  To
    /// be more precise, try findLiveValuesAcrossEdge.
    void findLiveInToBB(
      const BasicBlock *bb,         // input
      ValueList &liveIns) const;    // output

    /// Compute a vector of Value pointers
    /// representing values which are
    /// live-out of basic block bb
    void findLiveOutFromBB(
      const BasicBlock *bb,         // input
      ValueList &liveOuts) const;   // output

    /// Compute a vector of Value pointers
    /// representing values which are
    /// live immediately after the
    /// instructing inst.
    void findLiveValuesAfterInst(
      const Instruction *inst,      // input
      ValueList &liveValues) const; // output

    /// Compute a vector of Value pointers
    /// representing values which are
    /// live across the control edge
    /// (pred -> succ(pred,succno)
    /// This is similar, but more precise than
    /// findLiveInsToBB( succ(pred,succno) )
    void findLiveValuesAcrossEdge(
      const BasicBlock *pred,       // input
      unsigned succno,              // input
      ValueList &liveIns) const;    // output

  private:
    Value2Num numbers;
    Num2Value revNumbers;
    ValueSets OUT;

    void assignValueNumber(const Value *v);
    void computeIN(const BasicBlock *bb, BitVector &IN) const;
    void addUsesFromPHI(const BasicBlock *pred, const BasicBlock *succ, BitVector &IN) const;
  };
}

#endif // LLVM_LIBERTY_LIVE_VALUES_H
