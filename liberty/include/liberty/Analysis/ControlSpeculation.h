// This file defines an abstract interface named 'ControlSpeculation'
// This interface provides methods to query the effect of control
// speculation.  It does NOT tell you what to speculate.
//
// The policy of /what/ to speculate is implemented in  subclasses
// of ControlSpeculation, such as:
//  liberty::NoControlSpeculation and
//  liberty::SpecPriv::ProfileGuidedControlSpeculation.
#ifndef LLVM_LIBERTY_ANALYSIS_CONTROL_SPECULATION_H
#define LLVM_LIBERTY_ANALYSIS_CONTROL_SPECULATION_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

namespace liberty
{
using namespace llvm;

struct BBSuccIterator;
struct BBPredIterator;
struct LoopBBSuccIterator;
struct LoopBBPredIterator;

// liberty::Analysis::CtxInst, see liberty/Analysis/CallsiteSearch.h
struct CtxInst;

// liberty::Analysis::Context, see liberty/Analysis/CallsiteSearch.h
struct Context;

/// An abstraction of the control flow graph with dynamic
/// information about biased branches or dead blocks.
/// If at all possible, write your code to use this.
struct ControlSpeculation
{
  virtual ~ControlSpeculation() {}

  /// A block in relation to the Loop-CFG.
  struct LoopBlock
  {
    explicit LoopBlock(BasicBlock *block)
      : beforeIteration(false),
        loopContinue(false),
        loopExit(false),
        bb(block)
    {}

    // We need a default constructor, copy constructor, so that it
    // can be inserted into an STL collection.  This constructor
    // builds an 'invalid' loop block, which can be tested with
    // the isValid() method.
    LoopBlock()
      : beforeIteration(true),
        loopContinue(true),
        loopExit(true),
        bb(0)
    {}
    bool isValid() const;

    LoopBlock(const LoopBlock &other)
      : beforeIteration( other.beforeIteration ),
        loopContinue( other.loopContinue ),
        loopExit( other.loopExit ),
        bb( other.bb )
    {}

    static LoopBlock BeforeIteration()        { return LoopBlock(true,  false, false, 0); }
    static LoopBlock LoopContinue()           { return LoopBlock(false, true,  false, 0); }
    static LoopBlock LoopExit(BasicBlock *bb) { return LoopBlock(false, false, true,  bb); }

    bool isBeforeIteration() const { return beforeIteration; }
    bool isLoopContinue() const { return loopContinue; }
    bool isLoopExit() const { return loopExit; }
    bool isAfterIteration() const { return loopContinue || loopExit; }

    BasicBlock *getBlock() const { return bb; }

    bool operator==(const LoopBlock &other) const;
    bool operator!=(const LoopBlock &other) const { return !( (*this) == other ); }

    // So that it can be inserted into std::set or std::map
    bool operator<(const LoopBlock &other) const;

    void print(raw_ostream &fout) const;

  private:
    LoopBlock(bool before, bool cont, bool exit, BasicBlock *block)
      : beforeIteration(before),
        loopContinue(cont),
        loopExit(exit),
        bb(block) {}

    bool                beforeIteration;
    bool                loopContinue;
    bool                loopExit;

    BasicBlock        * bb;
  };

  // File results according to a given loop of interenst
  virtual void setLoopOfInterest(const BasicBlock *basic_block);
  const BasicBlock *getLoopHeaderOfInterest() const;

  // ------------------- Overload these two methods.

  // Determine if the provided control flow edge
  // is speculated to not run.
  virtual bool isSpeculativelyDead(const TerminatorInst *term, unsigned succNo) = 0;

  // Determine if the given basic block is speculatively dead.
  virtual bool isSpeculativelyDead(const BasicBlock *bb) = 0;

  // ------------------- CFG inspection methods

  // Cut some register uses in response to control speculation.
  bool phiUseIsSpeculativelyDead(const PHINode *phi, unsigned operandNumber);
  bool phiUseIsSpeculativelyDead(const PHINode *phi, const Instruction *operand);

  // Determine if this CtxInst is dead (this is a
  // CallsiteSearch CtxInst)
  bool  isSpeculativelyDead (const CtxInst &ci);

  // Determine if this CtxInst is dead (this is a
  // CallsiteSearch Context)
  bool  isSpeculativelyDead (const Context &ctx);

  // Determine if all edges control flow from A to B
  // are speculated not to run.
  bool isSpeculativelyDead(const BasicBlock *A, const BasicBlock *B);

  // Determine if the instruction is located in a speculaltively
  // dead basic block
  bool isSpeculativelyDead(const Instruction *inst);

  // Determine if the terminator instruction has a single
  // successor under the speculative assumption
  bool isSpeculativelyUnconditional(const TerminatorInst *term);

  // Iterate over successors of a basic block.
  typedef BBSuccIterator succ_iterator;
  succ_iterator succ_begin(BasicBlock *bb);
  succ_iterator succ_end(BasicBlock *bb);

  // Iterator over predecessors of a basic block.
  typedef BBPredIterator pred_iterator;
  pred_iterator pred_begin(BasicBlock *bb);
  pred_iterator pred_end(BasicBlock *bb);

  // ------------------- Path inspection methods

  // Is there a path from src to dst within a single iteration of loop?
  bool isReachable(Instruction *src, Instruction *dst, Loop *loop);

  // Is there a path from src to dst within a single iteration of loop,
  // AND WHICH exits src.  The second criterion is important when src==dst:
  // sometimes, a block can reach itself (e.g. if there is a nested-loop
  // which contains that block) and sometimes not.
  bool isReachable(BasicBlock *src, BasicBlock *dst, Loop *loop);

  // ------------------- Loop inspection methods

  typedef SmallVector<BasicBlock*,4> ExitingBlocks;
  typedef SmallVector<BasicBlock*,4> ExitBlocks;

  // Collect a list of basic blocks which may break from the loop
  // There should be one entry for each control-flow edge that
  // exits the loop; if a block has two exits (e.g. because of
  // a switch instruction), then that block will be listed twice.
  void getExitingBlocks(Loop *loop, ExitingBlocks &exitingBlocks);

  // Collect a list of loop exit blocks.  These are blocks *outside*
  // of the loop which have a predecessor *inside* the loop.
  void getExitBlocks(Loop *loop, ExitBlocks &exitBlocks);

  // In some cases, we may have speculated ALL exit edges
  // from the loop; i.e. the loop is specualtively infinite.
  // Detect that case.
  bool isInfinite(Loop *loop);

  /// If we speculate that this loop will never exercise its backedge.
  bool isNotLoop(Loop *loop);

  // Analogous to Loop::getExitingBlock()
  // Determine if (speculatively) there is a single block
  // within this loop which may exit the loop.
  // Return that block, or null on failure.
  BasicBlock *getExitingBlock(Loop *loop);

  // Analogous to Loop::getUniqueExitBlock()
  // Determine if (speculatively) there is a single block
  // which the loop exits to (i.e. the landing pad
  // outside of the loop).
  // Return that block, or null on failure.
  BasicBlock *getUniqueExitBlock(Loop *loop);

  // Determine if this conditional branch may exit the loop
  bool mayExit(TerminatorInst *term, Loop *loop);

  // Iterator over successors of a basic block in a LOOP CFG
  typedef LoopBBSuccIterator loop_succ_iterator;
  loop_succ_iterator succ_begin(Loop *l, LoopBlock lb);
  loop_succ_iterator succ_end(Loop *l, LoopBlock lb);

  // Iterator over predecessors of a basic block in a LOOP CFG
  typedef LoopBBPredIterator loop_pred_iterator;
  loop_pred_iterator pred_begin(Loop *l, LoopBlock lb);
  loop_pred_iterator pred_end(Loop *l, LoopBlock lb);

  // This is just an ugly technical detail for
  // wrestling with C++'s multiple inheritance :(
  virtual ControlSpeculation *getControlSpecPtr() { return this; }

  // Print the speculative CFG as a dot file.
  void to_dot(const Function *fcn, LoopInfo &li, raw_ostream &fout);

  void to_dot_group_by_loop(Loop *loop, raw_ostream &fout, std::set<BasicBlock*> &already, unsigned depth);
  virtual void dot_block_label(const BasicBlock *bb, raw_ostream &fout) const;
  virtual void dot_edge_label(const TerminatorInst *term, unsigned sn, raw_ostream &fout) const;

  virtual void reset() { reachableCache.clear(); }

private:
  struct ReachableKey
  {
    BasicBlock *src, *dst;
    Loop *loop;

    ReachableKey(BasicBlock *s, BasicBlock *d, Loop *l)
      : src(s), dst(d), loop(l) {}

    bool operator<(const ReachableKey &other) const
    {
      if( loop < other.loop )
        return true;
      else if( loop > other.loop )
        return false;

      else if( src < other.src )
        return true;
      else if( src > other.src )
        return false;

      else
        return dst < other.dst;
    }
  };
  typedef std::map<ReachableKey,bool> ReachableCache;

  ReachableCache reachableCache;

  const BasicBlock *loop_header;
};

/// The null implementation: no speculation.
struct NoControlSpeculation : public ControlSpeculation
{
  // Determine if the provided control flow edge
  // is speculated to not run.
  virtual bool isSpeculativelyDead(const TerminatorInst *term, unsigned succNo) { return false; }

  // Determine if the given basic block is speculatively dead.
  virtual bool isSpeculativelyDead(const BasicBlock *bb) { return false; }
};

raw_ostream &operator<<(raw_ostream &fout, const ControlSpeculation::LoopBlock &block);


}

#endif

