/* This file defines two utility classes:
 * ForwardSearch_CtrlSpecAware and ReverseSearch_CtrlSpecAware.
 * These search an instruction (possibly a callsite,
 * and all transitive callees) for operations which
 * may read from/write to memory, respectively.
 * They aggressively prune this list to be those
 * operations which may interact with operations
 * outside of the call tree.   These classes
 * are sort-of optimized for the access pattern
 * of Callsite-Depth-Combinator-AA.
 * Look there for an example of usage.
 *
 * Note about memory management:
 *  The instruction search classes produce
 *  CtxInst_CtrlSpecAware objects, which contain an instruction
 *  and a context.  A context is a smart-pointer
 *  reference to a CallsiteContext_CtrlSpecAware, and will persist
 *  as long as you hold a reference to them.
 *  However, the contexts produced by different
 *  instruction search objects are not folded.
 */
#ifndef LLVM_LIBERTY_CALLSITE_SEARCH_CTRL_SPEC_H
#define LLVM_LIBERTY_CALLSITE_SEARCH_CTRL_SPEC_H

#include "llvm/IR/Dominators.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Utilities/FindUnderlyingObjects.h"

#include <set>

namespace llvm
{
  class CallSite;
}

namespace liberty
{
  using namespace llvm;

  // See KillFlow_CtrlSpecAware.h
  class KillFlow_CtrlSpecAware;

  /// Represents the context in which we
  /// observed an instruction; effectively
  /// a list of nested callsites.
  struct CallsiteContext_CtrlSpecAware
  {
    CallsiteContext_CtrlSpecAware(const CallSite &call, CallsiteContext_CtrlSpecAware *within);

    void incref() { ++refcount; }
    void decref() { if( --refcount<1 ) delete this; }

    const Instruction *getLocationWithinParent() const { return cs.getInstruction(); }
    const CallSite &getCallSite() const { return cs; }
    const Function *getFunction() const { return cs.getCalledFunction(); }
    CallsiteContext_CtrlSpecAware *getParent() const { return parent; }

    void print(raw_ostream &out) const;

    bool operator==(const CallsiteContext_CtrlSpecAware &other) const;

  private:
    ~CallsiteContext_CtrlSpecAware();

    // Don't put these in STL collections.
    CallsiteContext_CtrlSpecAware() { assert(false); }
    CallsiteContext_CtrlSpecAware(const CallsiteContext_CtrlSpecAware &) { assert(false); }
    CallsiteContext_CtrlSpecAware &operator=(const CallsiteContext_CtrlSpecAware &) { assert(false); return *this; }

    const CallSite cs;
    CallsiteContext_CtrlSpecAware *parent;
    unsigned refcount;
  };

  struct Context_CtrlSpecAware
  {
    Context_CtrlSpecAware(CallsiteContext_CtrlSpecAware *csc = 0);

    // You can put these in STL collections.
    Context_CtrlSpecAware(const Context_CtrlSpecAware &other);
    Context_CtrlSpecAware &operator=(const Context_CtrlSpecAware &other);
    ~Context_CtrlSpecAware();

    Context_CtrlSpecAware getSubContext_CtrlSpecAware(const CallSite &cs) const;

    void print(raw_ostream &out) const;
    const CallsiteContext_CtrlSpecAware *front() const { return first; }

    /// Determine if this (ptr) is killed by instructions which
    /// occur (Before) or (!Before) the location (locInCtx)
    /// within this context.  This is relatively fast; it prunes
    /// private allocation units (allocas), against stores which
    /// must-alias with the pointer, and operations which must kill
    /// (ptr)'s underlying object (if the underlying object is unique).
    bool kills(
      KillFlow_CtrlSpecAware &kill,
      const Value *ptr,
      const Instruction *locInCtx,
      bool Before,
      bool PointerIsLocalToContext_CtrlSpecAware = false,
      time_t queryStart = 0, unsigned Timeout = 0) const;

    /// Compute the set of aggregate objects (objects) from which
    /// the pointer (ptr) may be defined, and which are not killed
    /// (Before) or (!Before) the location (locInCtx) within this
    /// context.  This is slower than kills() because it will track
    /// potentially many underlying objects (which may occur because of
    /// PHI nodes or SELECT instructions).
    void getUnderlyingObjects(
      KillFlow_CtrlSpecAware &kill,
      const Value *ptr,
      const Instruction *locInCtx,
      UO &objects,  // output
      bool Before) const;

    bool operator==(const Context_CtrlSpecAware &other) const;

  private:
    CallsiteContext_CtrlSpecAware *first;

    bool kills(
      KillFlow_CtrlSpecAware &kill,
      const Value *ptr, // pointer in question
      const Value *obj, // it's underlying object
      const Instruction *locInCtx,
      const CallsiteContext_CtrlSpecAware *ctx,
      bool Before,
      bool PointerIsLocalToContext_CtrlSpecAware,
      time_t queryStart=0, unsigned Timeout=0) const;

    void getUnderlyingObjects(
      KillFlow_CtrlSpecAware &kill,
      const Value *ptr,
      const Instruction *locInCtx,
      CallsiteContext_CtrlSpecAware *ctx,
      UO &objects,
      bool Before) const;
  };

  raw_ostream &operator<<(raw_ostream &out, const CallsiteContext_CtrlSpecAware &ctx);

  class PureFunAA;
  class SemiLocalFunAA;

  /// Represents an instruction in a specific
  /// context.  Provides convenience methods
  /// which recursively query KillFlow_CtrlSpecAware.
  struct CtxInst_CtrlSpecAware
  {
    // A magical, distinguished object
    // which refers to the hidden state
    // of system calls, library functions, etc.
    // This may appear in the return values
    // from getNonLocalFootprint.
    static const Value *IO;

    CtxInst_CtrlSpecAware(const Instruction *i, const Context_CtrlSpecAware &context);

    // You can put these in STL collections.
    CtxInst_CtrlSpecAware();
    CtxInst_CtrlSpecAware(const CtxInst_CtrlSpecAware &other);
    CtxInst_CtrlSpecAware &operator=(const CtxInst_CtrlSpecAware &other);

    void print(raw_ostream &out) const;

    const Instruction *getInst() const { return inst; }
    const Context_CtrlSpecAware &getContext() const { return ctx; }

    /// Return true if the store memory footprint of this
    /// operation may flow to operations outside of this
    /// context.
    bool isLiveOut(KillFlow_CtrlSpecAware &kill, time_t queryStart=0, unsigned Timeout=0) const;

    /// Return true if the load memory footprint of this
    /// operation may flow from operations outside of this
    /// context.
    bool isLiveIn(KillFlow_CtrlSpecAware &kill, time_t queryStart=0, unsigned Timeout=0) const;

    /// Compute the memory footprint of an CtxInst_CtrlSpecAware, in terms of
    /// an objects-read set and an objects-write set.
    /// Returns true if this is a complete list, of false otherwise.
    bool getNonLocalFootprint(
      KillFlow_CtrlSpecAware &kill, PureFunAA &pure, SemiLocalFunAA &semi,
      UO &readsIn,
      UO &writesOut) const;

    bool operator==(const CtxInst_CtrlSpecAware &other) const;

  private:
    const Instruction *inst;
    Context_CtrlSpecAware ctx;
  };

  raw_ostream &operator<<(raw_ostream &out, const CtxInst_CtrlSpecAware &ci);

  typedef std::vector<CtxInst_CtrlSpecAware> CtxIList;
  typedef std::pair<CtxInst_CtrlSpecAware, CtxInst_CtrlSpecAware> CIPair;
  typedef std::vector< CIPair > CIPairs;

  /// Iterator abstraction over the search
  struct InstSearch_CtrlSpecAware;
  struct InstSearch_CtrlSpecAwareIterator
  {
    InstSearch_CtrlSpecAwareIterator(bool isEndIterator) : search(0), offset(0) {}
    InstSearch_CtrlSpecAwareIterator(InstSearch_CtrlSpecAware *s) : search(s), offset(0) {}

    const CtxInst_CtrlSpecAware operator*() const;

    // prefix increment.
    InstSearch_CtrlSpecAwareIterator &operator++() { ++offset; return *this; }
    bool operator!=(const InstSearch_CtrlSpecAwareIterator &other) const { return !this->operator==(other); }
    bool operator==(const InstSearch_CtrlSpecAwareIterator &other) const;

  private:
    InstSearch_CtrlSpecAware *search;
    unsigned offset;

    bool isEndIterator() const { return search == 0; }
  };

  /// Represents the state of a depth-first
  /// search over instructions.
  struct InstSearch_CtrlSpecAware
  {
    typedef CtxIList Fringe;
    typedef std::set<const Instruction *> Visited;
    typedef InstSearch_CtrlSpecAwareIterator iterator;

    /// Reads means include instructions which may read from memory.
    /// Writes means include instructions which may write to memory.
    InstSearch_CtrlSpecAware(bool read, bool write, time_t queryStart=0, unsigned Timeout=0);
    virtual ~InstSearch_CtrlSpecAware() {}

    iterator begin();
    iterator end();

    bool isDone() const;

    virtual void tryGetMoreHits() = 0;

    const CtxInst_CtrlSpecAware &getHit(unsigned n) const;
    unsigned getNumHits() const;

  protected:
    /// Contains yet-to-be-explored instructions
    Fringe     fringe;

    /// Contains callsite instructions which have already
    /// been visited; avoids infinite recursion.
    Visited    visited;

    /// Contains all of the instructions of interest we
    /// have yet found.
    Fringe     hits;

    time_t     queryStart;
    unsigned   Timeout;

    bool mayReadWrite(const Instruction *inst) const;

  private:
    /// We are searching for reads and/or writes.
    const bool Reads, Writes;

    bool mayWriteToMemory(const Instruction *inst) const;
    bool mayReadFromMemory(const Instruction *inst) const;
  };

  /// Visit instructions in dominator-order
  struct ForwardSearch_CtrlSpecAware : public InstSearch_CtrlSpecAware
  {
    ForwardSearch_CtrlSpecAware(const Instruction *start, KillFlow_CtrlSpecAware &k, bool read, bool write, time_t queryStart=0, unsigned Timeout=0);
    virtual void tryGetMoreHits();

  private:
    KillFlow_CtrlSpecAware &kill;

    bool goal(const CtxInst_CtrlSpecAware &n);
    bool isGoalState(const CtxInst_CtrlSpecAware &n);
    void searchSuccessors();
    void expandSuccessors(const CtxInst_CtrlSpecAware &ci);
    void expandSuccessors(DomTreeNode *nn, const Context_CtrlSpecAware &ctx);
  };

  struct ForwardLoadSearch_CtrlSpecAware : public ForwardSearch_CtrlSpecAware
  {
    ForwardLoadSearch_CtrlSpecAware(const Instruction *start, KillFlow_CtrlSpecAware &k, time_t queryStart=0, unsigned Timeout=0) : ForwardSearch_CtrlSpecAware(start,k,true,false,queryStart,Timeout) {}
  };

  struct ForwardStoreSearch_CtrlSpecAware : public ForwardSearch_CtrlSpecAware
  {
    ForwardStoreSearch_CtrlSpecAware(const Instruction *start, KillFlow_CtrlSpecAware &k, time_t queryStart=0, unsigned Timeout=0) : ForwardSearch_CtrlSpecAware(start,k,false,true,queryStart,Timeout) {}
  };

  /// Visit instructions in post-dominator-order
  struct ReverseSearch_CtrlSpecAware : public InstSearch_CtrlSpecAware
  {
    ReverseSearch_CtrlSpecAware(const Instruction *start, KillFlow_CtrlSpecAware &k, bool read, bool write, time_t queryStart=0, unsigned Timeout=0);
    virtual void tryGetMoreHits();

  private:
    KillFlow_CtrlSpecAware &kill;

    bool goal(const CtxInst_CtrlSpecAware &n);
    bool isGoalState(const CtxInst_CtrlSpecAware &n);
    void searchPredecessors();
    void expandPredecessors(const CtxInst_CtrlSpecAware &ci);
    void expandPredecessors(DomTreeNode *nn, const Context_CtrlSpecAware &ctx);
    void expandRoots(const PostDominatorTree *pdt, const Context_CtrlSpecAware &ctx);
  };

  struct ReverseLoadSearch_CtrlSpecAware : public ReverseSearch_CtrlSpecAware
  {
    ReverseLoadSearch_CtrlSpecAware(const Instruction *start, KillFlow_CtrlSpecAware &k, time_t queryStart=0, unsigned Timeout=0) : ReverseSearch_CtrlSpecAware(start,k,true,false,queryStart,Timeout) {}
  };


  struct ReverseStoreSearch_CtrlSpecAware : public ReverseSearch_CtrlSpecAware
  {
    ReverseStoreSearch_CtrlSpecAware(const Instruction *start, KillFlow_CtrlSpecAware &k, time_t queryStart=0, unsigned Timeout=0) : ReverseSearch_CtrlSpecAware(start,k,false,true,queryStart,Timeout) {}
  };
}

#endif //LLVM_LIBERTY_CALLSITE_SEARCH_H
