#define DEBUG_TYPE "callsite-depth-combinator-aa"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"

#include "liberty/Analysis/AnalysisTimeout.h"
#include "liberty/Analysis/CallsiteDepthCombinator.h"
#include "liberty/Analysis/Introspection.h"
#include "liberty/Analysis/KillFlow.h"
#include "liberty/Utilities/CallSiteFactory.h"

#include <ctime>

namespace liberty
{
  using namespace llvm;

  STATISTIC(numHits,     "Num cache hits");
  STATISTIC(numEligible, "Num eligible");
  STATISTIC(numFlowTests,"Num flow tests");

  STATISTIC(numKillScalarStoreAfterSrc,   "Num flows killed: store scalar after src");
  STATISTIC(numKillScalarStoreBeforeDst,  "Num flows killed: store scalar before dst");
  STATISTIC(numKillScalarStoreBetween,    "Num flows killed: store scalar between src and dst");
  STATISTIC(numKillScalarStoreInLoadCtx,  "Num flows killed: store scalar within dst context");

  STATISTIC(numKillScalarLoadAfterSrc,    "Num flows killed: load scalar after src");
  STATISTIC(numKillScalarLoadBeforeDst,   "Num flows killed: load scalar before dst");
  STATISTIC(numKillScalarLoadBetween,     "Num flows killed: load scalar between src and dst");
  STATISTIC(numKillScalarLoadInStoreCtx,  "Num flows killed: load scalar within src context");

  STATISTIC(numKillAggregateLoad,         "Num flows killed: load from killed aggregate");
  STATISTIC(numKillAggregateStore,        "Num flows killed: store to killed aggregate");

  bool CallsiteDepthCombinator::runOnModule(Module &mod)
  {
    const DataLayout &DL = mod.getDataLayout();
    InitializeLoopAA(this, DL);

    killflow = getAnalysisIfAvailable< KillFlow >();
    if( !killflow )
    {
      errs() << "KillFlow not available, creating a private instance.\n";
      killflow = new KillFlow();
      killflow->setEffectiveNextAA( getNextAA() );
      killflow->setEffectiveTopAA( getTopAA() );
      killflow->setModuleLoops( & getAnalysis< ModuleLoops >() );
      killflow->setDL(&DL);
      //killflow->setProxy(this);
    }


    return false;
  }

  bool CallsiteDepthCombinator::isEligible(const Instruction *i) const
  {
    CallSite cs = getCallSite(i);
    if( !cs.getInstruction() )
      return false;

    const Function *f = cs.getCalledFunction();
    if( !f )
      return false;

    if( f->isDeclaration() )
      return false;

    return true;
  }

  void CallsiteDepthCombinator::getAnalysisUsage(AnalysisUsage &AU) const
  {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired< ModuleLoops >();
    //AU.addRequired< DominatorTreeWrapperPass >();
    //AU.addRequired< PostDominatorTreeWrapperPass >();
    //AU.addRequired< LoopInfoWrapperPass >();
//    AU.addRequired< KillFlow >();
    AU.setPreservesAll();        // Does not transform code
  }

  static const Instruction *getToplevelInst(const CtxInst &ci)
  {
    const Instruction *inst = ci.getInst();
    for(const CallsiteContext *ctx = ci.getContext().front(); ctx; ctx=ctx->getParent() )
      inst = ctx->getLocationWithinParent();

    return inst;
  }

  /// Determine if it is possible for a store
  /// 'src' to flow to a load 'dst' across
  /// the backedge of L.
  bool CallsiteDepthCombinator::mayFlowCrossIter(
    const CtxInst &write, const CtxInst &read, const Loop *L, KillFlow &kill,
    time_t queryStart, unsigned Timeout)
  {
    const Instruction *src = getToplevelInst(write),
                      *dst = getToplevelInst(read);

    return mayFlowCrossIter(kill,src,dst,L,write,read,queryStart,Timeout);
  }

  bool CallsiteDepthCombinator::mayFlowCrossIter(
    KillFlow &kill,
    const Instruction *src,
    const Instruction *dst,
    const Loop *L,
    const CtxInst &write,
    const CtxInst &read,
    time_t queryStart,unsigned Timeout)
  {
    ++numFlowTests;

    LoopAA *top = kill.getTopAA();
    INTROSPECT(errs() << "Test flow from " << write << " to " << read << " {\n");
//      enterIntrospectionRegion(false);
    ModRefResult q = top->modref(write.getInst(), Before, read.getInst(), L);
//      exitIntrospectionRegion();
    INTROSPECT(errs() << "} Exit test flow +--> " << q << '\n');
    if( q == NoModRef || q == Ref )
      return false;

    // May have been a flow.
    // Try to prove that the flow was killed.
    INTROSPECT(errs() << "CallsiteDepthIterator: Maybe flow\n"
                      << "  from: " << write << '\n'
                      << "    to: " << read << '\n');

    INTROSPECT(errs() << "- s0\n");

    // Was a store killed between the two operations?
    if( const StoreInst *store = dyn_cast< StoreInst >(write.getInst()) )
    {
      const Value *ptr = store->getPointerOperand();
      if( kill.pointerKilledAfter(L, ptr, src, true, queryStart, Timeout) )
      {
        ++numKillScalarStoreAfterSrc;
        return false;
      }

      INTROSPECT(errs() << "- s0.1\n");

      if( kill.pointerKilledBefore(L, ptr, dst, true, queryStart, Timeout) )
      {
        ++numKillScalarStoreBeforeDst;
        return false;
      }

      INTROSPECT(errs() << "- s0.2\n");

      if( read.getContext().kills(kill, ptr, read.getInst(), true, false, queryStart, Timeout) )
      {
        ++numKillScalarStoreInLoadCtx;
        return false;
      }
    }

    INTROSPECT(errs() << "- s1\n");

    // Was a load killed between the two operations?
    if( const LoadInst *load = dyn_cast< LoadInst >(read.getInst()) )
    {
      const Value *ptr = load->getPointerOperand();
      if( kill.pointerKilledBefore(L, ptr, dst, true, queryStart, Timeout) )
      {
        ++numKillScalarLoadBeforeDst;
        return false;
      }

      INTROSPECT(errs() << "- s1.1\n");

      if( kill.pointerKilledAfter(L, ptr, src, true, queryStart, Timeout) )
      {
        ++numKillScalarLoadAfterSrc;
        return false;
      }

      INTROSPECT(errs() << "- s1.2\n");

      if( write.getContext().kills(kill, ptr, write.getInst(), false, false, queryStart, Timeout) )
      {
        ++numKillScalarLoadInStoreCtx;
        return false;
      }
    }

    INTROSPECT(errs() << "- s2\n");

    // Were the store's underlying objects killed?
    if( const StoreInst *store = dyn_cast< StoreInst >(write.getInst()) )
    {
      const Value *ptr = store->getPointerOperand();
      bool allObjectsKilled = true;
      UO objects;
      write.getContext().getUnderlyingObjects(kill,ptr,write.getInst(),objects,false);
      for(UO::iterator i=objects.begin(), e=objects.end(); i!=e; ++i)
      {
        const Value *object = *i;

        if( kill.aggregateKilledAfter(L, object, src, queryStart, Timeout) )
          continue;
        if( kill.aggregateKilledBefore(L, object, dst, queryStart, Timeout) )
          continue;

        allObjectsKilled = false;
        break;
      }

      if( allObjectsKilled )
      {
        ++numKillAggregateStore;
        return false;
      }
    }

    INTROSPECT(errs() << "- s3\n");

    // Were the load's underlying objects killed?
    if( const LoadInst *load = dyn_cast< LoadInst >(read.getInst()) )
    {
      const Value *ptr = load->getPointerOperand();
      bool allObjectsKilled = true;
      UO objects;
      read.getContext().getUnderlyingObjects(kill,ptr,read.getInst(),objects,true);
      for(UO::iterator i=objects.begin(), e=objects.end(); i!=e; ++i)
      {
        const Value *object = *i;

        if( kill.aggregateKilledAfter(L, object, src, queryStart, Timeout) )
          continue;
        if( kill.aggregateKilledBefore(L, object, dst, queryStart, Timeout) )
          continue;

        allObjectsKilled = false;
        break;
      }

      if( allObjectsKilled )
      {
        ++numKillAggregateLoad;
        return false;
      }
    }

    INTROSPECT(errs() << "- s4\n");

    return true;
  }

  bool CallsiteDepthCombinator::mayFlowIntraIter(
    KillFlow &kill,
    const Instruction *src,
    const Instruction *dst,
    const Loop *L,
    const CtxInst &write,
    const CtxInst &read)
  {
    ++numFlowTests;

    LoopAA *top = kill.getTopAA();
    INTROSPECT(errs() << "Test flow from " << write << " to " << read << " {\n");
//      enterIntrospectionRegion(false);
    ModRefResult q = top->modref(write.getInst(), Same, read.getInst(), L);
//      exitIntrospectionRegion();
    INTROSPECT(errs() << "} Exit test flow +--> " << q << '\n');
    if( q == NoModRef || q == Ref )
      return false;

    // May have been a flow.
    // Try to prove that the flow was killed.
    INTROSPECT(errs() << "CallsiteDepthIterator: Maybe flow\n"
                      << "  from: " << write << '\n'
                      << "    to: " << read << '\n');

    INTROSPECT(errs() << "- s0\n");

    // Was a store killed between the two operations?
    if( const StoreInst *store = dyn_cast< StoreInst >(write.getInst()) )
    {
      const Value *ptr = store->getPointerOperand();
      if( kill.pointerKilledBetween(L, ptr, src, dst) )
      {
        ++numKillScalarStoreBetween;
        return false;
      }

      INTROSPECT(errs() << "- s0.1\n");

      if( read.getContext().kills(kill, ptr, read.getInst(), true) )
      {
        ++numKillScalarStoreInLoadCtx;
        return false;
      }
    }

    INTROSPECT(errs() << "- s1\n");

    // Was a load killed between the two operations?
    if( const LoadInst *load = dyn_cast< LoadInst >(read.getInst()) )
    {
      const Value *ptr = load->getPointerOperand();
      if( kill.pointerKilledBetween(L, ptr, src,dst) )
      {
        ++numKillScalarLoadBetween;
        return false;
      }

      INTROSPECT(errs() << "- s1.1\n");

      if( write.getContext().kills(kill, ptr, write.getInst(), false) )
      {
        ++numKillScalarLoadInStoreCtx;
        return false;
      }
    }

    INTROSPECT(errs() << "- s2\n");

    // Were the store's underlying objects killed?
    if( const StoreInst *store = dyn_cast< StoreInst >(write.getInst()) )
    {
      const Value *ptr = store->getPointerOperand();
      bool allObjectsKilled = true;
      UO objects;
      write.getContext().getUnderlyingObjects(kill,ptr,write.getInst(),objects,false);
      for(UO::iterator i=objects.begin(), e=objects.end(); i!=e; ++i)
      {
        const Value *object = *i;

        if( kill.aggregateKilledBetween(L, object, src, dst) )
          continue;

        allObjectsKilled = false;
        break;
      }

      if( allObjectsKilled )
      {
        ++numKillAggregateStore;
        return false;
      }
    }

    INTROSPECT(errs() << "- s3\n");

    // Were the load's underlying objects killed?
    if( const LoadInst *load = dyn_cast< LoadInst >(read.getInst()) )
    {
      const Value *ptr = load->getPointerOperand();
      bool allObjectsKilled = true;
      UO objects;
      read.getContext().getUnderlyingObjects(kill,ptr,read.getInst(),objects,true);
      for(UO::iterator i=objects.begin(), e=objects.end(); i!=e; ++i)
      {
        const Value *object = *i;

        if( kill.aggregateKilledBetween(L, object, src, dst) )
          continue;

        allObjectsKilled = false;
        break;
      }

      if( allObjectsKilled )
      {
        ++numKillAggregateLoad;
        return false;
      }
    }

    INTROSPECT(errs() << "- s4\n");

    return true;
  }

  bool CallsiteDepthCombinator::doFlowSearchCrossIter(
    const Instruction *src,
    const Instruction *dst,
    const Loop *L,
    KillFlow &kill,
    CCPairs *allFlowsOut,
    time_t queryStart, unsigned Timeout)
  {

    ReverseStoreSearch writes(src,kill,queryStart,Timeout);
    INTROSPECT(
      errs() << "LiveOuts {\n";
      // List all live-outs and live-ins.
      // This is really inefficient; a normal
      // query enumerates only as many as are necessary
      // before it witnesses a flow.
      for(InstSearch::iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
      {
        const CtxInst &write = *i;
        errs() << "LiveOut(" << *src << ") write: " << write << '\n';
      }
      errs() << "}\n";
    );

    return doFlowSearchCrossIter(src,dst,L, writes,kill,allFlowsOut,queryStart, Timeout);
  }

  bool CallsiteDepthCombinator::doFlowSearchCrossIter(
    const Instruction *src,
    const Instruction *dst,
    const Loop *L,
    InstSearch &writes,
    KillFlow &kill,
    CCPairs *allFlowsOut,
    time_t queryStart, unsigned Timeout)
  {
    ForwardLoadSearch reads(dst,kill,queryStart,Timeout);
    INTROSPECT(
      errs() << "LiveIns {\n";

      for(InstSearch::iterator j=reads.begin(), f=reads.end(); j!=f; ++j)
      {
        const CtxInst &read = *j;
        errs() << "LiveIn(" << *dst << ") read: " << read << '\n';
      }

      errs() << "}\n";
    );

    return doFlowSearchCrossIter(src,dst,L, writes,reads, kill,allFlowsOut,queryStart, Timeout);
  }

  bool CallsiteDepthCombinator::doFlowSearchCrossIter(
    const Instruction *src,
    const Instruction *dst,
    const Loop *L,
    InstSearch &writes,
    InstSearch &reads,
    KillFlow &kill,
    CCPairs *allFlowsOut,
    time_t queryStart,
    unsigned Timeout)
  {
    const bool stopAfterFirst = (allFlowsOut == 0);
    bool isFlow = false;

    // Not yet in cache.  Look it up.
    for(InstSearch::iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
    {
      const CtxInst &write = *i;
//        errs() << "Write: " << write << '\n';

      for(InstSearch::iterator j=reads.begin(), f=reads.end(); j!=f; ++j)
      {
        const CtxInst &read = *j;
//          errs() << "  Read: " << read << '\n';

        if(Timeout > 0 && queryStart > 0)
        {
          time_t now;
          time(&now);
          if( (now - queryStart) > Timeout )
          {
            errs() << "CDC::doFlowSearchCrossIter Timeout\n";
            return true;
          }
        }

        if( !mayFlowCrossIter(kill, src,dst,L, write,read, queryStart, Timeout) )
          continue;

        // TODO
        // Is there some way that the ReverseStoreSearch
        // and ForwardLoadSearch can (i) first, return
        // unexpanded callsites, and (ii) allow this loop
        // to ask them to expand those callsites, if
        // necessary, but (iii) Leave those callsites
        // unexpanded for later iterations of the loop?

        INTROSPECT(
          errs() << "Can't disprove flow\n"
                 << "\tfrom: " << write << '\n'
                 << "\t  to: " << read  << '\n');
        DEBUG(
          errs() << "Can't disprove flow\n"
                 << "\tfrom: " << write << '\n'
                 << "\t  to: " << read  << '\n');

        if( allFlowsOut )
          allFlowsOut->push_back( CCPair(write,read) );
        isFlow = true;

        if( stopAfterFirst && isFlow )
          break;
      }

      if( stopAfterFirst && isFlow )
        break;
    }

    return isFlow;
  }

  LoopAA::ModRefResult CallsiteDepthCombinator::modref(
    const Instruction *inst1,
    TemporalRelation Rel,
    const Instruction *inst2,
    const Loop *L)
  {
    ModRefResult result = LoopAA::modref(inst1,Rel,inst2,L);
    if( result == NoModRef || result == Ref )
      return result;
    if( Rel == Same )
      return result;
    if( !L->contains(inst1) || !L->contains(inst2) )
      return result;
    if( !isEligible(inst1) && !isEligible(inst2) )
      return result;

    if( !inst1->mayReadFromMemory() )
      result = ModRefResult(result & ~Ref);
    if( !inst1->mayWriteToMemory() )
      return ModRefResult(result & ~Mod);

    const Instruction *src=inst1, *dst=inst2;
    if( Rel == After )
      std::swap(src,dst);

    if( !src->mayWriteToMemory()
    ||  !dst->mayReadFromMemory() )
      return result;

    // Maybe turn-on introspection
    bool introspect = false;
    if( WatchCallsitePair )
    {
      CallSite cs1 = getCallSite(inst1),
               cs2 = getCallSite(inst2);
      if( cs1.getInstruction() && cs2.getInstruction() )
        if( const Function *f1 = cs1.getCalledFunction() )
          if( const Function *f2 = cs2.getCalledFunction() )
            if( f1->getName() == FirstCallee )
              if( f2->getName() == SecondCallee )
                introspect = true;
    }

    else if( WatchCallsite2Store )
    {
      CallSite cs1 = getCallSite(inst1);
      const StoreInst *st2 = dyn_cast< StoreInst >(inst2);

      if( cs1.getInstruction() && st2 )
        if( const Function *f1 = cs1.getCalledFunction() )
          if( f1->getName() == FirstCallee )
            if( st2->getPointerOperand()->getName() == StorePtrName )
              introspect = true;
    }

    else if( WatchStore2Callsite )
    {
      const StoreInst *st1 = dyn_cast< StoreInst >(inst1);
      CallSite cs2 = getCallSite(inst2);

      if( cs2.getInstruction() && st1 )
        if( const Function *f2 = cs2.getCalledFunction() )
          if( f2->getName() == SecondCallee )
            if( st1->getPointerOperand()->getName() == StorePtrName )
              introspect = true;
    }

    if( introspect )
      enterIntrospectionRegion();

    INTROSPECT(ENTER(inst1,Rel,inst2,L));
    INTROSPECT(errs() << "Starting with " << result << '\n');

    ++numEligible;

    // This analysis is trying to find
    // a flow of values through memory.
    bool isFlow = false;

    // Cached result?
    IIKey key(src,Before,dst,L);
    if( iiCache.count(key) )
    {
      // Use result from cache.
      ++numHits;
      isFlow = iiCache[key];
    }

    else
    {
      time_t queryStart=0;
      if( AnalysisTimeout > 0 )
        time(&queryStart);
      isFlow = iiCache[key] = doFlowSearchCrossIter(src,dst, L,*killflow, 0,queryStart, AnalysisTimeout);
      queryStart = 0;
    }

    // Interpret the isFlow result w.r.t. LoopAA Before/After query semantics.
    if( !isFlow )
    {
      DEBUG(errs() << "No flow from " << *src << " to " << *dst << '\n');

      if( Rel == Before )
        result = ModRefResult(result & ~Mod);

      else if( Rel == After )
        result = ModRefResult(result & ~Ref);
    }

    INTROSPECT(EXIT(inst1,Rel,inst2,L,result));
    if( introspect )
      exitIntrospectionRegion();
    return result;
  }

  LoopAA::ModRefResult CallsiteDepthCombinator::modref(
    const Instruction *i1,
    TemporalRelation Rel,
    const Value *p2,
    unsigned s2,
    const Loop *L)
  {
    ModRefResult result = LoopAA::modref(i1,Rel,p2,s2,L);
    if( result == NoModRef || result == Ref )
      return result;
    if( Rel == Same )
      return result;
    if( !L->contains(i1) )
      return result;
    if( !isEligible(i1) )
      return result;
    INTROSPECT( ENTER(i1,Rel,p2,s2,L) );

    // TODO

    INTROSPECT( EXIT(i1,Rel,p2,s2,L,result) );
    return result;
  }

  char CallsiteDepthCombinator::ID = 0;

  static RegisterPass<CallsiteDepthCombinator>
  XX("callsite-depth-combinator-aa", "Alias analysis with deep inspection of callsites", false, true);
  static RegisterAnalysisGroup<liberty::LoopAA> Y(XX);


}

