/* This file defines two utility classes:
 * ForwardSearch and ReverseSearch.
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
 *  CtxInst objects, which contain an instruction
 *  and a context.  A context is a smart-pointer
 *  reference to a CallsiteContext, and will persist
 *  as long as you hold a reference to them.
 *  However, the contexts produced by different
 *  instruction search objects are not folded.
 */
#ifndef LLVM_LIBERTY_CALLSITE_SEARCH_H
#define LLVM_LIBERTY_CALLSITE_SEARCH_H

#include "llvm/IR/Dominators.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Utilities/FindUnderlyingObjects.h"

#include "Assumptions.h"

#include <set>
#include <map>

namespace llvm
{
  class CallSite;
}

namespace liberty
{
  using namespace llvm;

  // See KillFlow.h
  class KillFlow;
  class PureFunAA;
  class SemiLocalFunAA;

  /// Represents the context in which we
  /// observed an instruction; effectively
  /// a list of nested callsites.
  struct CallsiteContext
  {
    CallsiteContext(const CallSite &call, CallsiteContext *within);

    void incref() { ++refcount; }
    void decref() { if( --refcount<1 ) delete this; }

    const Instruction *getLocationWithinParent() const { return cs.getInstruction(); }
    const CallSite &getCallSite() const { return cs; }
    const Function *getFunction() const { return cs.getCalledFunction(); }
    CallsiteContext *getParent() const { return parent; }

    void print(raw_ostream &out) const;

    bool operator==(const CallsiteContext &other) const;
    bool operator<(const CallsiteContext &other) const;

  private:
    ~CallsiteContext();

    // Don't put these in STL collections.
    CallsiteContext() { assert(false); }
    CallsiteContext(const CallsiteContext &) { assert(false); }
    CallsiteContext &operator=(const CallsiteContext &) { assert(false); return *this; }

    const CallSite cs;
    CallsiteContext *parent;
    unsigned refcount;
  };

  struct Context
  {
    Context(CallsiteContext *csc = 0);

    // You can put these in STL collections.
    Context(const Context &other);
    Context &operator=(const Context &other);
    ~Context();

    Context getSubContext(const CallSite &cs) const;

    void print(raw_ostream &out) const;
    const CallsiteContext *front() const { return first; }

    /// Determine if this (ptr) is killed by instructions which
    /// occur (Before) or (!Before) the location (locInCtx)
    /// within this context.  This is relatively fast; it prunes
    /// private allocation units (allocas), against stores which
    /// must-alias with the pointer, and operations which must kill
    /// (ptr)'s underlying object (if the underlying object is unique).
    bool kills(
      KillFlow &kill,
      const Value *ptr,
      const Instruction *locInCtx,
      bool Before,
      bool PointerIsLocalToContext = false,
      time_t queryStart = 0, unsigned Timeout = 0) const;

    /// Compute the set of aggregate objects (objects) from which
    /// the pointer (ptr) may be defined, and which are not killed
    /// (Before) or (!Before) the location (locInCtx) within this
    /// context.  This is slower than kills() because it will track
    /// potentially many underlying objects (which may occur because of
    /// PHI nodes or SELECT instructions).
    void getUnderlyingObjects(
      KillFlow &kill,
      const Value *ptr,
      const Instruction *locInCtx,
      UO &objects,  // output
      bool Before) const;

    bool operator==(const Context &other) const;
    bool operator<(const Context &other) const;

  private:
    CallsiteContext *first;

    bool kills(
      KillFlow &kill,
      const Value *ptr, // pointer in question
      const Value *obj, // it's underlying object
      const Instruction *locInCtx,
      const CallsiteContext *ctx,
      bool Before,
      bool PointerIsLocalToContext,
      time_t queryStart=0, unsigned Timeout=0) const;

    void getUnderlyingObjects(
      KillFlow &kill,
      const Value *ptr,
      const Instruction *locInCtx,
      CallsiteContext *ctx,
      UO &objects,
      bool Before) const;
  };

  raw_ostream &operator<<(raw_ostream &out, const CallsiteContext &ctx);

  class PureFunAA;
  class SemiLocalFunAA;

  /// Represents an instruction in a specific
  /// context.  Provides convenience methods
  /// which recursively query KillFlow.
  struct CtxInst
  {
    // A magical, distinguished object
    // which refers to the hidden state
    // of system calls, library functions, etc.
    // This may appear in the return values
    // from getNonLocalFootprint.
    static const Value *IO;

    CtxInst(const Instruction *i, const Context &context);

    // You can put these in STL collections.
    CtxInst();
    CtxInst(const CtxInst &other);
    CtxInst &operator=(const CtxInst &other);

    void print(raw_ostream &out) const;

    const Instruction *getInst() const { return inst; }
    const Context &getContext() const { return ctx; }

    /// Return true if the store memory footprint of this
    /// operation may flow to operations outside of this
    /// context.
    bool isLiveOut(KillFlow &kill, time_t queryStart=0, unsigned Timeout=0) const;

    /// Return true if the load memory footprint of this
    /// operation may flow from operations outside of this
    /// context.
    bool isLiveIn(KillFlow &kill, time_t queryStart=0, unsigned Timeout=0) const;

    /// Compute the memory footprint of an CtxInst, in terms of
    /// an objects-read set and an objects-write set.
    /// Returns true if this is a complete list, of false otherwise.
    bool getNonLocalFootprint(
      KillFlow &kill, PureFunAA &pure, SemiLocalFunAA &semi,
      UO &readsIn,
      UO &writesOut) const;

    bool operator==(const CtxInst &other) const;

    bool operator<(const CtxInst &other) const;

  private:
    const Instruction *inst;
    Context ctx;
  };

  raw_ostream &operator<<(raw_ostream &out, const CtxInst &ci);

  typedef std::vector<CtxInst> CIList;
  typedef std::pair<CtxInst, CtxInst> CCPair;
  typedef std::vector< CCPair > CCPairs;
  typedef std::map<CCPair, Remedies> CCPairsRemedsMap;

  /// Iterator abstraction over the search
  struct InstSearch;
  struct InstSearchIterator
  {
    InstSearchIterator(bool isEndIterator) : search(0), offset(0) {}
    InstSearchIterator(InstSearch *s) : search(s), offset(0) {}

    const CtxInst operator*() const;

    // prefix increment.
    InstSearchIterator &operator++() { ++offset; return *this; }
    bool operator!=(const InstSearchIterator &other) const { return !this->operator==(other); }
    bool operator==(const InstSearchIterator &other) const;

  private:
    InstSearch *search;
    unsigned offset;

    bool isEndIterator() const { return search == 0; }
  };

  /// Represents the state of a depth-first
  /// search over instructions.
  struct InstSearch
  {
    typedef CIList Fringe;
    typedef std::set<const Instruction *> Visited;
    typedef InstSearchIterator iterator;

    /// Reads means include instructions which may read from memory.
    /// Writes means include instructions which may write to memory.
    InstSearch(bool read, bool write, time_t queryStart=0, unsigned Timeout=0, PureFunAA *pure=nullptr, SemiLocalFunAA *semi=nullptr);
    virtual ~InstSearch() {}

    iterator begin();
    iterator end();

    bool isDone() const;

    virtual void tryGetMoreHits() = 0;

    const CtxInst &getHit(unsigned n) const;
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
    PureFunAA *pure;
    SemiLocalFunAA *semi;

    bool mayReadWrite(const Instruction *inst) const;

  private:
    /// We are searching for reads and/or writes.
    const bool Reads, Writes;

    bool mayWriteToMemory(const Instruction *inst) const;
    bool mayReadFromMemory(const Instruction *inst) const;
  };

  /// Visit instructions in dominator-order
  struct ForwardSearch : public InstSearch
  {
    ForwardSearch(const Instruction *start, KillFlow &k, bool read, bool write, time_t queryStart=0, unsigned Timeout=0, PureFunAA *pure=nullptr, SemiLocalFunAA *semi=nullptr);
    virtual void tryGetMoreHits();

  private:
    KillFlow &kill;

    bool goal(const CtxInst &n);
    bool isGoalState(const CtxInst &n);
    void searchSuccessors();
    void expandSuccessors(const CtxInst &ci);
    void expandSuccessors(DomTreeNode *nn, const Context &ctx);
  };

  struct ForwardLoadSearch : public ForwardSearch
  {
    ForwardLoadSearch(const Instruction *start, KillFlow &k, time_t queryStart=0, unsigned Timeout=0, PureFunAA *pure=nullptr,SemiLocalFunAA *semi=nullptr) : ForwardSearch(start,k,true,false,queryStart,Timeout, pure, semi) {}
  };

  struct ForwardStoreSearch : public ForwardSearch
  {
    ForwardStoreSearch(const Instruction *start, KillFlow &k, time_t queryStart=0, unsigned Timeout=0, PureFunAA *pure=nullptr, SemiLocalFunAA *semi=nullptr) : ForwardSearch(start,k,false,true,queryStart,Timeout, pure, semi) {}
  };

  /// Visit instructions in post-dominator-order
  struct ReverseSearch : public InstSearch
  {
    ReverseSearch(const Instruction *start, KillFlow &k, bool read, bool write, time_t queryStart=0, unsigned Timeout=0, PureFunAA *pure=nullptr, SemiLocalFunAA *semi=nullptr);
    virtual void tryGetMoreHits();

  private:
    KillFlow &kill;

    bool goal(const CtxInst &n);
    bool isGoalState(const CtxInst &n);
    void searchPredecessors();
    void expandPredecessors(const CtxInst &ci);
    void expandPredecessors(DomTreeNode *nn, const Context &ctx);
    void expandRoots(const PostDominatorTree *pdt, const Context &ctx);
  };

  struct ReverseLoadSearch : public ReverseSearch
  {
    ReverseLoadSearch(const Instruction *start, KillFlow &k, time_t queryStart=0, unsigned Timeout=0, PureFunAA *pure=nullptr, SemiLocalFunAA *semi=nullptr) : ReverseSearch(start,k,true,false,queryStart,Timeout, pure, semi) {}
  };


  struct ReverseStoreSearch : public ReverseSearch
  {
    ReverseStoreSearch(const Instruction *start, KillFlow &k, time_t queryStart=0, unsigned Timeout=0, PureFunAA *pure=nullptr, SemiLocalFunAA *semi=nullptr) : ReverseSearch(start,k,false,true,queryStart,Timeout, pure, semi) {}
  };
}

#endif //LLVM_LIBERTY_CALLSITE_SEARCH_H

