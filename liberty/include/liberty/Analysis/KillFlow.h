#ifndef LLVM_LIBERTY_KILL_FLOW_H
#define LLVM_LIBERTY_KILL_FLOW_H

#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Utilities/ModuleLoops.h"

namespace liberty
{
  class KillFlow : public ModulePass, public LoopAA
  {
    typedef std::pair<const Function *, const Value *> FcnPtrPair;
    typedef DenseMap< FcnPtrPair, bool > FcnKills;

    typedef std::pair<const BasicBlock *, const Value *> BBPtrPair;
    typedef DenseMap< BBPtrPair, bool > BBKills;

    // We can summarize functions in terms of
    // which values they kill.
    FcnKills fcnKills;

    // And we can summarize BBs in the same way
    BBKills bbKills;

    // Hold reference to this.
    ModuleLoops *mloops;
    //Pass *proxy;

    // Allow the client to set this (i.e. KillFlow does not need to run as a pass)
    LoopAA *effectiveNextAA;
    LoopAA *effectiveTopAA;

    bool mustAlias(const Value *storeptr, const Value *loadptr);
    bool mustAliasFast(const Value *, const Value *, const DataLayout &DL);

    /// Determine if this instruction MUST KILL the specified pointer.
    bool instMustKill(const Instruction *inst, const Value *ptr, time_t queryStart, unsigned Timeout);

    /// Determine if the block MUST KILL the specified pointer.
    /// If <after> belongs to this block and <after> is not null, only consider operations AFTER <after>
    /// If <after> belongs to this block and <before> is is not null, only consider operations BEFORE <before>
    bool blockMustKill(const BasicBlock *bb, const Value *ptr, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout);

    /// Determine if this instruction MUST KILL the specified <aggregate>
    bool instMustKillAggregate(const Instruction *inst, const Value *aggregate, time_t queryStart, unsigned Timeout);

    /// Determine if the block MUST KILL the specified aggregate
    /// If <after> belongs to this block and <after> is not null, only consider operations AFTER <after>
    /// If <after> belongs to this block and <before> is is not null, only consider operations BEFORE <before>
    bool blockMustKillAggregate(const BasicBlock *bb, const Value *aggregate, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout);

    bool allLoadsAreKilledBefore(const Loop *L, CallSite &cs, time_t queryStart, unsigned Timeout);

    const DataLayout *DL;

  protected:
    virtual void uponStackChange();

  public:
    static char ID;
    KillFlow();
    ~KillFlow();

    bool runOnModule(Module &M)
    {
      DL = &M.getDataLayout();
      InitializeLoopAA(this, *DL);
      setModuleLoops( & getAnalysis< ModuleLoops >() );
      //setProxy(this);
      return false;
    }


    void setModuleLoops(ModuleLoops *ml)
    {
      mloops = ml;
    }

    /*
    void setProxy(Pass* p)
    {
      proxy = p;
    }
    */

    StringRef getLoopAAName() const { return "kill-flow-aa"; }

    void getAnalysisUsage(AnalysisUsage &AU) const
    {
      LoopAA::getAnalysisUsage(AU);
      AU.addRequired< ModuleLoops >();
      //AU.addRequired< DominatorTreeWrapperPass >();
      //AU.addRequired< PostDominatorTreeWrapperPass >();
      //AU.addRequired< LoopInfoWrapperPass >();
      AU.setPreservesAll();
    }

    void setEffectiveTopAA(LoopAA *top)
    {
      effectiveTopAA = top;
    }
    void setEffectiveNextAA(LoopAA *next)
    {
      effectiveNextAA = next;
    }

    LoopAA *getEffectiveTopAA() const
    {
      if( effectiveTopAA )
        return effectiveTopAA;
      else
        return getTopAA();
    }

    LoopAA *getEffectiveNextAA() const
    {
      if( effectiveNextAA )
        return effectiveNextAA;
      else
        return getNextAA();
    }

    /// getAdjustedAnalysisPointer - This method is used when a pass implements
    /// an analysis interface through multiple inheritance.  If needed, it
    /// should override this to adjust the this pointer as needed for the
    /// specified pass info.
    virtual void *getAdjustedAnalysisPointer(AnalysisID PI)
    {
      if (PI == &LoopAA::ID)
        return (LoopAA*)this;
      return this;
    }

    ModRefResult modref(const Instruction *i1,
                        TemporalRelation Rel,
                        const Value *p2,
                        unsigned sz2,
                        const Loop *L)
    {
      return LoopAA::modref(i1,Rel,p2,sz2,L);
    }


    ModRefResult modref(const Instruction *i1,
                        TemporalRelation Rel,
                        const Instruction *i2,
                        const Loop *L);

    /// Determine if there is an operation in <L> which must execute before <before> which kills <ptr>
    bool pointerKilledBefore(const Loop *L, const Value *ptr, const Instruction *before, bool alsoCheckAggregate=true, time_t queryStart=0, unsigned Timeout=0);

    /// Determine if there is an operation in <L> which must execute after <after> which kills <ptr>
    bool pointerKilledAfter(const Loop *L, const Value *ptr, const Instruction *after, bool alsoCheckAggregate=true, time_t queryStart=0, unsigned Timeout=0);

    /// Determine if there is an operation in <L> which must execute
    /// after <after> and before <before> which kills <ptr>
    bool pointerKilledBetween(const Loop *L, const Value *ptr, const Instruction *after, const Instruction *before, bool alsoCheckAggregate=true, time_t queryStart=0, unsigned Timeout=0);

    /// Determine if there is an operation in <L> which must execute before <before> which kills the aggregate
    bool aggregateKilledBefore(const Loop *L, const Value *obj, const Instruction *before, time_t queryStart=0, unsigned Timeout=0);

    /// Determine if there is an operation in <L> which must execute after <after> which kills the aggregate
    bool aggregateKilledAfter(const Loop *L, const Value *obj, const Instruction *after, time_t queryStart=0, unsigned Timeout=0);

    /// Determine if there is an operation in <L> which must execute
    /// after <after> and before <before> which kills the aggregate
    bool aggregateKilledBetween(const Loop *L, const Value *obj, const Instruction *after, const Instruction *before, time_t queryStart=0, unsigned Timeout=0);

    const PostDominatorTree *getPDT(const Function *cf);
    const DominatorTree *getDT(const Function *cf);
  };

}

#endif // LLVM_LIBERTY_KILL_FLOW_H

