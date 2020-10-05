#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Analysis/AnalysisTimeout.h"
#include "liberty/Analysis/Introspection.h"
#include "liberty/Analysis/PureFunAA.h"
#include "liberty/Analysis/SemiLocalFunAA.h"
#include "liberty/Speculation/CallsiteSearch_CtrlSpecAware.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
#include "scaf/Utilities/CallSiteFactory.h"

namespace liberty
{
  using namespace llvm;

  #define INCREF(x)   do { if(x) x->incref(); } while(0)
  #define DECREF(x)   do { if(x) x->decref(); } while(0)

  CallsiteContext_CtrlSpecAware::CallsiteContext_CtrlSpecAware(const CallSite &call, CallsiteContext_CtrlSpecAware *within)
    : cs(call), parent(within), refcount(0) { INCREF(parent); }

  void CallsiteContext_CtrlSpecAware::print(raw_ostream &out) const
  {
    if( parent != 0 )
      parent->print(out);
    out << ">>" << getFunction()->getName();
  }

  CallsiteContext_CtrlSpecAware::~CallsiteContext_CtrlSpecAware() { DECREF(parent); }

  bool CallsiteContext_CtrlSpecAware::operator==(const CallsiteContext_CtrlSpecAware &other) const
  {
    if( this == &other )
      return true;
    if( 0 == this )
      return false;
    if( 0 == &other )
      return false;

    if( this->cs.getInstruction() != other.cs.getInstruction() )
      return false;

    return *(this->parent) == *(other.parent);
  }

  Context_CtrlSpecAware::Context_CtrlSpecAware(CallsiteContext_CtrlSpecAware *csc)
    : first(csc) { INCREF(first); }

  Context_CtrlSpecAware::Context_CtrlSpecAware(const Context_CtrlSpecAware &other)
    : first( other.first ) { INCREF(first); }

  Context_CtrlSpecAware::~Context_CtrlSpecAware() { DECREF(first); }

  Context_CtrlSpecAware &Context_CtrlSpecAware::operator=(const Context_CtrlSpecAware &other)
  {
    INCREF(other.first);
    DECREF(first);
    first = other.first;
    return *this;
  }

  Context_CtrlSpecAware Context_CtrlSpecAware::getSubContext_CtrlSpecAware(const CallSite &cs) const
  {
    CallsiteContext_CtrlSpecAware *ctx = new CallsiteContext_CtrlSpecAware(cs,first);
    return Context_CtrlSpecAware(ctx);
  }

  bool Context_CtrlSpecAware::kills(KillFlow_CtrlSpecAware &kill, const Value *ptr, const Instruction *locInCtx, bool Before, bool PointerIsLocalToContext_CtrlSpecAware, time_t queryStart, unsigned Timeout) const
  {
    return kills(kill, ptr, ptr, locInCtx, front(), Before, PointerIsLocalToContext_CtrlSpecAware, queryStart, Timeout);
  }

  bool Context_CtrlSpecAware::kills(KillFlow_CtrlSpecAware &kill, const Value *ptr, const Value *obj, const Instruction *locInCtx, const CallsiteContext_CtrlSpecAware *ctx, bool Before, bool PointerIsLocalToContext_CtrlSpecAware, time_t queryStart, unsigned Timeout) const
  {
    // No more context to kill stuff
    if( ctx == 0 )
      return false;

    // If this ptr is a context-private allocation unit, it cannot escape.
    if( const AllocaInst *alloca = dyn_cast< AllocaInst >(ptr) )
      if( alloca->getParent()->getParent() == ctx->getFunction() )
      {
        INTROSPECT(
          errs() << "Context_CtrlSpecAware::kills(" << *ptr << ") is an alloca in ";
          ctx->print(errs());
          errs() << '\n';);
        return true;
      }

    // If this aggregate is a context-private allocation unit, it cannot escape.
    const Module *M = locInCtx->getParent()->getParent()->getParent();
    const DataLayout &DL = M->getDataLayout();
    obj = GetUnderlyingObject(obj,DL,0);
    if( const AllocaInst *alloca = dyn_cast< AllocaInst >(obj) )
      if( alloca->getParent()->getParent() == ctx->getFunction() )
      {
        INTROSPECT(
          errs() << "context::kills(" << *ptr << ") underlying object " << *obj << " is an alloca in ";
          ctx->print(errs());
          errs() << '\n';);
        return true;
      }

    // Before recurring, check for timeout.
    if(Timeout > 0 && queryStart > 0)
    {
      time_t now;
      time(&now);
      if( (now - queryStart) > Timeout )
      {
        errs() << "Context_CtrlSpecAware::kills Timeout\n";
        return false;
      }
    }

    // Determine if there are stores which kill flow to/from this pointer
    if( Before )
    {
      if( kill.pointerKilledBefore(0, ptr, locInCtx, false, queryStart, Timeout) )
      {
        INTROSPECT(
          errs() << "context::kills(" << *ptr << ") is killed before in ";
          ctx->print(errs());
          errs() << '\n';);
        return true;
      }

      if( kill.aggregateKilledBefore(0, obj, locInCtx, queryStart, Timeout) )
      {
        INTROSPECT(
          errs() << "context::kills(" << *ptr << ") underlying object " << *obj << " is killed before in ";
          ctx->print(errs());
          errs() << '\n';);
        return true;
      }
    }

    else // if after
    {
      if( kill.pointerKilledAfter(0, ptr, locInCtx, false, queryStart, Timeout) )
      {
        INTROSPECT(
          errs() << "context::kills(" << *ptr << ") is killed after in ";
          ctx->print(errs());
          errs() << '\n';);
        return true;
      }

      if( kill.aggregateKilledAfter(0, obj, locInCtx, queryStart, Timeout) )
      {
        INTROSPECT(
          errs() << "context::kills(" << *ptr << ") underlying object " << *obj << " is killed after in ";
          ctx->print(errs());
          errs() << '\n';);
        return true;
      }
    }

    // Possibly replace formal parameter with actual parameter.
    if( PointerIsLocalToContext_CtrlSpecAware )
    {
      if( const Argument *arg = dyn_cast< Argument >(ptr) )
        if( arg->getParent() == ctx->getCallSite().getCalledFunction() )
          ptr = ctx->getCallSite().getArgument( arg->getArgNo() );
      if( const Argument *arg = dyn_cast< Argument >(obj) )
        if( arg->getParent() == ctx->getCallSite().getCalledFunction() )
          obj = ctx->getCallSite().getArgument( arg->getArgNo() );
    }

    // Before recurring, check for timeout.
    if(Timeout > 0 && queryStart > 0)
    {
      time_t now;
      time(&now);
      if( (now - queryStart) > Timeout )
      {
        errs() << "Context_CtrlSpecAware::kills Timeout\n";
        return false;
      }
    }

    return kills(kill, ptr, obj, ctx->getLocationWithinParent(), ctx->getParent(), Before, PointerIsLocalToContext_CtrlSpecAware, queryStart, Timeout);
  }

  void Context_CtrlSpecAware::getUnderlyingObjects(KillFlow_CtrlSpecAware &kill, const Value *ptr, const Instruction *locInCtx, UO &objects, bool Before) const
  {
    getUnderlyingObjects(kill, ptr, locInCtx, first, objects, Before);
  }

  void Context_CtrlSpecAware::getUnderlyingObjects(KillFlow_CtrlSpecAware &kill, const Value *ptr, const Instruction *locInCtx, CallsiteContext_CtrlSpecAware *ctx, UO &objects, bool Before) const
  {

    //sot
    const Module *M = locInCtx->getModule();
    const DataLayout &DL = M->getDataLayout();

    UO objs;
    GetUnderlyingObjects(ptr,objs,DL);

    for(UO::iterator i=objs.begin(), e=objs.end(); i!=e; ++i)
    {
      const Value *obj = *i;
      if( objects.count(obj) )
        continue;

      if( !ctx )
      {
        objects.insert(obj);
        continue;
      }

      // If this aggregate is a context-private allocation unit, it cannot escape.
      if( const AllocaInst *alloca = dyn_cast< AllocaInst >(obj) )
        if( alloca->getParent()->getParent() == ctx->getFunction() )
          continue;

      // Skip if killed.
      if( Before && kill.aggregateKilledBefore(0, obj, locInCtx) )
        continue;

      if( !Before && kill.aggregateKilledAfter(0, obj, locInCtx) )
        continue;

      // Substitute formal parameters
      if( const Argument *arg = dyn_cast< Argument >(obj) )
        if( arg->getParent() == ctx->getCallSite().getCalledFunction() )
          obj = ctx->getCallSite().getArgument( arg->getArgNo() );

      // And recur.
      getUnderlyingObjects(
        kill,
        obj,
        ctx->getLocationWithinParent(),
        ctx->getParent(),
        objects,
        Before);
    }
  }

  void Context_CtrlSpecAware::print(raw_ostream &out) const
  {
    if( front() )
      front()->print(out);
  }

  bool Context_CtrlSpecAware::operator==(const Context_CtrlSpecAware &other) const
  {
    return *(this->first) == *(other.first);
  }

  raw_ostream &operator<<(raw_ostream &out, const Context_CtrlSpecAware &ctx)
  {
    ctx.print(out);
    return out;
  }

  const Value *CtxInst_CtrlSpecAware::IO = (const Value*) ~0UL;

  CtxInst_CtrlSpecAware::CtxInst_CtrlSpecAware(const Instruction *i, const Context_CtrlSpecAware &context)
    : inst(i), ctx(context) {}

  // You can put these in STL collections.
  CtxInst_CtrlSpecAware::CtxInst_CtrlSpecAware() : inst(0), ctx(0) {}
  CtxInst_CtrlSpecAware::CtxInst_CtrlSpecAware(const CtxInst_CtrlSpecAware &other)
    : inst(other.inst), ctx(other.ctx) {}
  CtxInst_CtrlSpecAware &CtxInst_CtrlSpecAware::operator=(const CtxInst_CtrlSpecAware &other)
  {
    this->inst = other.inst;
    this->ctx = other.ctx;

    return *this;
  }

  void CtxInst_CtrlSpecAware::print(raw_ostream &out) const
  {
    ctx.print(out);
    out << *getInst();
  }

  /// Return true if the store memory footprint of this
  /// operation may flow to operations outside of this
  /// context.
  bool CtxInst_CtrlSpecAware::isLiveOut(KillFlow_CtrlSpecAware &kill, time_t queryStart, unsigned Timeout) const
  {
    if( const StoreInst *store = dyn_cast< StoreInst >(inst) )
      return !getContext().kills(kill, store->getPointerOperand(), store, false, true, queryStart, Timeout);

    return true;
  }

  /// Return true if the load memory footprint of this
  /// operation may flow from operations outside of this
  /// context.
  bool CtxInst_CtrlSpecAware::isLiveIn(KillFlow_CtrlSpecAware &kill, time_t queryStart, unsigned Timeout) const
  {
    if( const LoadInst *load = dyn_cast< LoadInst >(inst) )
      return !getContext().kills(kill, load->getPointerOperand(), load, true, true, queryStart, Timeout);

    return true;
  }

  bool CtxInst_CtrlSpecAware::getNonLocalFootprint(
    KillFlow_CtrlSpecAware &kill, PureFunAA &pure, SemiLocalFunAA &semi,
    UO &readsIn,
    UO &writesOut) const
  {
    // Cases:
    //  (1) load scalar
    //  (2) store scalar
    //  (3) llvm memory intrinsics:
    //    (a) memcpy
    //    (b) memmove
    //    (c) memset
    //  (4) external functions

    bool complete = true;
    const Context_CtrlSpecAware &ctx = getContext();
    const Instruction *inst = getInst();

    if( const LoadInst *load = dyn_cast< LoadInst >(inst) )
    {
      const Value *ptr = load->getPointerOperand();
      ctx.getUnderlyingObjects(kill,ptr,load,readsIn,true);
    }
    else if( const StoreInst *store = dyn_cast< StoreInst >(inst) )
    {
      const Value *ptr = store->getPointerOperand();
      ctx.getUnderlyingObjects(kill,ptr,store,writesOut,false);
    }
    else if( const MemIntrinsic *mem = dyn_cast< MemIntrinsic >(inst) )
    {
      if( mem->getIntrinsicID() == Intrinsic::memcpy
      ||  mem->getIntrinsicID() == Intrinsic::memmove )
      {
        ctx.getUnderlyingObjects(kill, mem->getOperand(1), mem, readsIn, true);
        ctx.getUnderlyingObjects(kill, mem->getOperand(0), mem, writesOut, false);
      }
      else if( mem->getIntrinsicID() == Intrinsic::memset )
      {
        ctx.getUnderlyingObjects(kill, mem->getOperand(0), mem, writesOut, false);
      }
      else
        assert(false && "Unknown memory intrinsic");
    }
    else
    {
      CallSite cs = getCallSite(inst);
      assert( cs.getInstruction() && "WTF");

      Function *f = cs.getCalledFunction();
      if( !f )
        complete = false;
      else if( semi.isSemiLocal(f,pure) )
      {
        readsIn.insert(IO);
        writesOut.insert(IO);
      }
      else if( !pure.isLocal(f) )
        complete = false;

      assert( f->isDeclaration() && "Callsite search should have expanded internally-defined function");

      for(unsigned i=0, n=cs.arg_size(); i<n; ++i)
      {
        bool readsArg = true, writesArg = true;

        if( f )
        {
          if( semi.writeOnlyFormalArg(f,i) )
            readsArg = false;

          if( pure.isReadOnly(f) )
            writesArg = false;
          else if( semi.readOnlyFormalArg(f,i) )
            writesArg = false;
        }

        const Value *v = cs.getArgument(i);
        if( readsArg )
          ctx.getUnderlyingObjects(kill,v,inst,readsIn,true);
        if( writesArg )
          ctx.getUnderlyingObjects(kill,v,inst,writesOut,false);
      }
    }

    return complete;
  }

  bool CtxInst_CtrlSpecAware::operator==(const CtxInst_CtrlSpecAware &other) const
  {
    return (this->inst == other.inst)
    &&     (this->ctx  == other.ctx);
  }

  raw_ostream &operator<<(raw_ostream &out, const CtxInst_CtrlSpecAware &ci)
  {
    ci.print(out);
    return out;
  }

  InstSearch_CtrlSpecAware::InstSearch_CtrlSpecAware(bool read, bool write, time_t start, unsigned t_o)
  : fringe(), visited(), hits(), queryStart(start), Timeout(t_o), Reads(read), Writes(write)
  {
    assert( Reads || Writes );
  }

  InstSearch_CtrlSpecAware::iterator InstSearch_CtrlSpecAware::begin() { return InstSearch_CtrlSpecAwareIterator(this); }
  InstSearch_CtrlSpecAware::iterator InstSearch_CtrlSpecAware::end()   { return InstSearch_CtrlSpecAwareIterator(true); }

  bool InstSearch_CtrlSpecAware::isDone() const { return fringe.empty(); }


  bool InstSearch_CtrlSpecAware::mayReadWrite(const Instruction *inst) const
  {
    return (Reads && mayReadFromMemory(inst))
    ||     (Writes && mayWriteToMemory(inst));
  }

  bool InstSearch_CtrlSpecAware::mayReadFromMemory(const Instruction *inst) const
  {
    if( !inst->mayReadFromMemory() )
      return false;

    if (const IntrinsicInst *intrinsic = dyn_cast<IntrinsicInst>(inst)) {
      if (intrinsic->getIntrinsicID() == Intrinsic::lifetime_start ||
          intrinsic->getIntrinsicID() == Intrinsic::lifetime_end ||
          intrinsic->getIntrinsicID() == Intrinsic::invariant_start ||
          intrinsic->getIntrinsicID() == Intrinsic::invariant_end)
        return false;
    }

    CallSite cs = getCallSite(inst);
    if( cs.getInstruction() )
    {
      if( isa< DbgInfoIntrinsic >(inst) )
        return false;

      if( isa< MemSetInst >(inst) )
        return false;

      // TODO: this is ugly; should instead query
      // the Pure Function list...
      if( const Function *callee = cs.getCalledFunction() )
      {
        const StringRef  name = callee->getName();
        if( name == "llvm.lifetime.start"
        ||  name == "llvm.lifetime.end"
        ||  name == "llvm.invariant.start"
        ||  name == "llvm.invariant.end"
        ||  name == "llvm.var.annotation"
        ||  name == "llvm.annotation.i8"
        ||  name == "llvm.annotation.i16"
        ||  name == "llvm.annotation.i32"
        ||  name == "llvm.annotation.i64"
        ||  name == "llvm.objectsize.i32"
        ||  name == "llvm.objectsize.i64"
        ||  name == "llvm.va_start"
        ||  name == "llvm.va_end"
        )
          return false;
      }
    }

    return true;
  }

  bool InstSearch_CtrlSpecAware::mayWriteToMemory(const Instruction *inst) const
  {
    if( !inst->mayWriteToMemory() )
      return false;

    if (const IntrinsicInst *intrinsic = dyn_cast<IntrinsicInst>(inst)) {
      if (intrinsic->getIntrinsicID() == Intrinsic::lifetime_start ||
          intrinsic->getIntrinsicID() == Intrinsic::lifetime_end ||
          intrinsic->getIntrinsicID() == Intrinsic::invariant_start ||
          intrinsic->getIntrinsicID() == Intrinsic::invariant_end)
        return false;
    }

    CallSite cs = getCallSite(inst);
    if( cs.getInstruction() )
    {
      if( isa< DbgInfoIntrinsic >(inst) )
        return false;

      // TODO: this is ugly; should instead query
      // the Pure Function list...
      if( const Function *callee = cs.getCalledFunction() )
      {
        const StringRef  name = callee->getName();
        if( name == "llvm.lifetime.start"
        ||  name == "llvm.lifetime.end"
        ||  name == "llvm.invariant.start"
        ||  name == "llvm.invariant.end"
        ||  name == "llvm.var.annotation"
        ||  name == "llvm.annotation.i8"
        ||  name == "llvm.annotation.i16"
        ||  name == "llvm.annotation.i32"
        ||  name == "llvm.annotation.i64"
        ||  name == "llvm.objectsize.i32"
        ||  name == "llvm.objectsize.i64"
        ||  name == "llvm.va_start"
        ||  name == "llvm.va_end"
        )
          return false;
      }

    }
    return true;
  }


  const CtxInst_CtrlSpecAware &InstSearch_CtrlSpecAware::getHit(unsigned n) const { return hits[n]; }
  unsigned InstSearch_CtrlSpecAware::getNumHits() const { return hits.size(); }

  const CtxInst_CtrlSpecAware InstSearch_CtrlSpecAwareIterator::operator*() const
  {
    assert(!isEndIterator() && "Dereference end iterator!");
    while( offset >= search->getNumHits() )
    {
      assert(!search->isDone() && "Dereference end iterator");
      search->tryGetMoreHits();
    }

    return search->getHit(offset);
  }

  bool InstSearch_CtrlSpecAwareIterator::operator==(const InstSearch_CtrlSpecAwareIterator &other) const
  {
    if( other.isEndIterator() )
    {
      while( offset >= search->getNumHits() )
      {
        if( search->isDone() )
          return true;
        search->tryGetMoreHits();
      }
      return false;
    }

    return this->search == other.search
    &&     this->offset == other.offset;
  }

  ForwardSearch_CtrlSpecAware::ForwardSearch_CtrlSpecAware(const Instruction *start, KillFlow_CtrlSpecAware &k, bool read, bool write, time_t queryStart, unsigned Timeout)
    : InstSearch_CtrlSpecAware(read,write,queryStart,Timeout), kill(k)
  {
    // Initialize the fringe.
    CtxInst_CtrlSpecAware s0(start, 0);
    isGoalState(s0);
  }

  void ForwardSearch_CtrlSpecAware::tryGetMoreHits() { searchSuccessors(); }

  bool ForwardSearch_CtrlSpecAware::goal(const CtxInst_CtrlSpecAware &n)
  {
    if( !n.isLiveIn(kill, queryStart, Timeout) )
      return false;
    hits.push_back(n);
    return true;
  }

  bool ForwardSearch_CtrlSpecAware::isGoalState(const CtxInst_CtrlSpecAware &n)
  {
    const Instruction *inst = n.getInst();
    CallSite cs = getCallSite(inst);
    if( cs.getInstruction() )
    {
      const Function *callee = cs.getCalledFunction();

      // indirect calls
      if( !callee )
        return goal(n);

      // calls to external functions
      if( callee->isDeclaration() )
      {
        if( mayReadWrite(inst) )
          return goal(n);
        else
          return false;
      }

      // Direct calls to defined functions
      else
      {
        // Avoid infinite search
        if( visited.count( inst ) )
          return false;
        visited.insert(inst);

        // Find the entry of the callee
        Context_CtrlSpecAware c2 = n.getContext().getSubContext_CtrlSpecAware(cs);
        CtxInst_CtrlSpecAware csucc( &callee->front().front(), c2 );

        fringe.push_back(csucc);
        return false;
      }
    }

    // Any non-callsite which may read or write
    else if( mayReadWrite(inst) )
      return goal(n);

    return false;
  }

  void ForwardSearch_CtrlSpecAware::searchSuccessors()
  {
    // Search_CtrlSpecAware locally for the next goal nodes
    while( ! fringe.empty() )
    {
      const CtxInst_CtrlSpecAware n = fringe.back();
      fringe.pop_back();

      // Check for goal state
      const bool foundAtLeastOneMore = isGoalState(n);

      expandSuccessors(n);

      if( foundAtLeastOneMore )
        break;
    }
  }

  void ForwardSearch_CtrlSpecAware::expandSuccessors(const CtxInst_CtrlSpecAware &ci)
  {
    // Either inst is the last in its basic block, or not.
    const Instruction *inst = ci.getInst();
    const BasicBlock *bb = inst->getParent();
    if( inst == bb->getTerminator() )
    {
      // Last in block.
      // Find later blocks using the dominator tree.
      const DominatorTree *dt = kill.getDT( bb->getParent() );
      DomTreeNode *nn = dt->getNode( const_cast<BasicBlock*>(bb) );

      expandSuccessors(nn, ci.getContext());
    }

    else
    {
      // Not last in block.
      const Instruction *succ = inst->getNextNode();
      CtxInst_CtrlSpecAware csucc(succ, ci.getContext());
      fringe.push_back(csucc);
    }
  }

  void ForwardSearch_CtrlSpecAware::expandSuccessors(DomTreeNode *nn, const Context_CtrlSpecAware &ctx)
  {
    for(DomTreeNode::iterator i=nn->begin(), e=nn->end(); i!=e; ++i)
    {
      const Instruction *succ = & (*i)->getBlock()->front();
      CtxInst_CtrlSpecAware csucc(succ, ctx);
      fringe.push_back(csucc);
    }
  }


  ReverseSearch_CtrlSpecAware::ReverseSearch_CtrlSpecAware(const Instruction *start, KillFlow_CtrlSpecAware &k, bool read, bool write, time_t queryStart, unsigned Timeout)
    : InstSearch_CtrlSpecAware(read,write,queryStart,Timeout), kill(k)
  {
    // Initialize the fringe.
    CtxInst_CtrlSpecAware s0(start, 0);
    isGoalState(s0);
  }

  void ReverseSearch_CtrlSpecAware::tryGetMoreHits() { searchPredecessors(); }

  bool ReverseSearch_CtrlSpecAware::goal(const CtxInst_CtrlSpecAware &n)
  {
    if( !n.isLiveOut(kill, queryStart, Timeout) )
      return false;
    hits.push_back(n);
    return true;
  }

  bool ReverseSearch_CtrlSpecAware::isGoalState(const CtxInst_CtrlSpecAware &n)
  {
    const Instruction *inst = n.getInst();
    CallSite cs = getCallSite(inst);
    if( cs.getInstruction() )
    {
      const Function *callee = cs.getCalledFunction();

      // indirect calls
      if( !callee )
        return goal(n);

      // calls to external functions
      if( callee->isDeclaration() )
      {
        if( mayReadWrite(inst) )
          return goal(n);
        else
          return false;
      }

      // Direct calls to defined functions
      else
      {
        // Avoid infinite search
        if( visited.count( inst ) )
          return false;
        visited.insert(inst);

        // Find the exits of the callee
        const PostDominatorTree *pdt = kill.getPDT(callee);
        const Context_CtrlSpecAware c2 = n.getContext().getSubContext_CtrlSpecAware(cs);
        expandRoots(pdt, c2);

        return false;
      }
    }

    // Any non-callsite which may read or write
    else if( mayReadWrite(inst) )
      return goal(n);

    return false;
  }

  void ReverseSearch_CtrlSpecAware::searchPredecessors()
  {
    // Search_CtrlSpecAware locally for the next goal nodes
    while( ! fringe.empty() )
    {
      const CtxInst_CtrlSpecAware n = fringe.back();
      fringe.pop_back();

      // Check for goal state
      const bool foundAtLeastOneMore = isGoalState(n);

      expandPredecessors(n);

      if( foundAtLeastOneMore )
        break;
    }
  }

  void ReverseSearch_CtrlSpecAware::expandPredecessors(const CtxInst_CtrlSpecAware &ci)
  {
    // Either inst is the first in its basic block, or not.
    const Instruction *inst = ci.getInst();
    const BasicBlock *bb = inst->getParent();
    if( inst == & bb->front() )
    {
      // First in block.
      // Find earlier blocks using the post-dominator tree.
      const PostDominatorTree *pdt = kill.getPDT( bb->getParent() );
      DomTreeNode *nn = pdt->getNode( const_cast<BasicBlock*>(bb) );

      expandPredecessors(nn, ci.getContext());
    }

    else
    {
      // Not first in block.
      const Instruction *pred = inst->getPrevNode();
      //ilist_nextprev_traits<Instruction>::getPrev(inst);
      CtxInst_CtrlSpecAware cpred(pred, ci.getContext());
      fringe.push_back(cpred);
    }
  }

  void ReverseSearch_CtrlSpecAware::expandPredecessors(DomTreeNode *nn, const Context_CtrlSpecAware &ctx)
  {
    for(DomTreeNode::iterator i=nn->begin(), e=nn->end(); i!=e; ++i)
    {
      const BasicBlock *bbpred = (*i)->getBlock();
      const Instruction *pred = bbpred->getTerminator();

      if( isa< UnreachableInst >(pred) )
        continue;

      CtxInst_CtrlSpecAware cpred(pred, ctx);
      fringe.push_back(cpred);
    }
  }

  void ReverseSearch_CtrlSpecAware::expandRoots(const PostDominatorTree *pdt, const Context_CtrlSpecAware &ctx)
  {
    // When a function has a unique exit,
    // llvm's PDT uses that as a root.
    // Otherwise, it creates a magical
    // root node with no block.  We must
    // handle both of those cases here.
    //
    // PDG::getRoots(), on the other hand,
    // returns all nodes which may exit the
    // function (e.g. return, unwind or unreachable),
    // however there are nodes which are not
    // post-dominated by the roots, and thus
    // we would miss some basic blocks if we
    // start from those.
    DomTreeNode *root = (DomTreeNode *) pdt->getRootNode();
    if( root->getBlock() )
    {
      const BasicBlock *bbpred = root->getBlock();
      const Instruction *pred = bbpred->getTerminator();
      if( isa< UnreachableInst >(pred) )
        return;

      CtxInst_CtrlSpecAware cpred(pred, ctx);
      fringe.push_back(cpred);
      return;
    }

    expandPredecessors(root,ctx);
  }
}
