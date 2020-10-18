#ifndef LLVM_LIBERTY_KILL_FLOW_CTRL_AWARE_H
#define LLVM_LIBERTY_KILL_FLOW_CTRL_AWARE_H

#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"

#include "scaf/Utilities/ControlSpeculation.h"
#include "scaf/MemoryAnalysisModules/FindSource.h"
#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/Utilities/LoopDominators.h"
#include "scaf/Utilities/ModuleLoops.h"

namespace liberty
{
using namespace SpecPriv;

  class KillFlow_CtrlSpecAware : public ModulePass, public LoopAA
  {
    typedef std::pair<const Function *, const Value *> FcnPtrPair;
    typedef DenseMap< FcnPtrPair, bool > FcnKills;

    typedef std::pair<const BasicBlock *, const Value *> BBPtrPair;
    typedef DenseMap< BBPtrPair, bool > BBKills;

    typedef std::pair<const Instruction *, const Instruction *> InstPtrPair;
    typedef DenseMap<InstPtrPair, bool> NoStoresBetween;

    // We can summarize functions in terms of
    // which values they kill.
    FcnKills fcnKills;

    // And we can summarize BBs in the same way
    BBKills bbKills;

    NoStoresBetween noStoresBetween;

    DenseMap<const BasicBlock *, SmallPtrSet<const Instruction *, 1>>
        loopKillAlongInsts;

    // Hold reference to this.
    ModuleLoops *mloops;
    const TargetLibraryInfo *tli;
    //Pass *proxy;

    // Allow the client to set this (i.e. KillFlow_CtrlSpecAware does not need to run as a pass)
    LoopAA *effectiveNextAA;
    LoopAA *effectiveTopAA;

    bool mustAlias(const Value *storeptr, const Value *loadptr);
    bool mustAliasFast(const Value *, const Value *, const DataLayout &DL);

    /// Determine if this instruction MUST KILL the specified <aggregate>
    bool instMustKillAggregate(const Instruction *inst, const Value *aggregate, time_t queryStart, unsigned Timeout);

    /// Determine if the block MUST KILL the specified aggregate
    /// If <after> belongs to this block and <after> is not null, only consider operations AFTER <after>
    /// If <after> belongs to this block and <before> is is not null, only consider operations BEFORE <before>
    bool blockMustKillAggregate(const BasicBlock *bb, const Value *aggregate, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout);

    bool allLoadsAreKilledBefore(const Loop *L, CallSite &cs, time_t queryStart, unsigned Timeout);

    BasicBlock *getLoopEntryBB(const Loop *loop);
    bool aliasBasePointer(const Value *gepptr, const Value *killgepptr,
                          const GlobalValue **gvSrc, ScalarEvolution *se,
                          const Loop *L);
    bool killAllIdx(const Value *killidx, const Value *basePtr,
                    const GlobalValue *gvSrc, ScalarEvolution *se,
                    const Loop *L, const Loop *innerL, unsigned idxID);
    bool matchingIdx(const Value *idx, const Value *killidx,
                     ScalarEvolution *se, const Loop *L);
    bool greaterThan(const SCEV *killTripCount, const SCEV *tripCount,
                     ScalarEvolution *se, const Loop *L);

    const DataLayout *DL;

    LoopDom *specDT;
    LoopPostDom *specPDT;
    Loop *tgtLoop;

  protected:
    virtual void uponStackChange();

  public:
    static char ID;
    KillFlow_CtrlSpecAware();
    ~KillFlow_CtrlSpecAware();

    bool runOnModule(Module &M)
    {
      DL = &M.getDataLayout();
      InitializeLoopAA(this, *DL);
      setModuleLoops( & getAnalysis< ModuleLoops >() );
      tli = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
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

    void setDL(const DataLayout *d)
    {
      DL = d;
    }

    void setLoopOfInterest(ControlSpeculation *cs, Loop *L) {
      //if (specDT)
      //  delete specDT;
      //if (specPDT)
      //  delete specPDT;

      if (!L) {
        specDT = nullptr;
        specPDT = nullptr;
        tgtLoop = nullptr;
        return;
      }

      cs->setLoopOfInterest(L->getHeader());
      specDT = new LoopDom(*cs, L);
      specPDT = new LoopPostDom(*cs, L);
      tgtLoop = L;
    }

    virtual SchedulingPreference getSchedulingPreference() const { return SchedulingPreference( Low - 1 ); }

    StringRef getLoopAAName() const { return "kill-flow-ctrl-spec-aa"; }

    void getAnalysisUsage(AnalysisUsage &AU) const
    {
      LoopAA::getAnalysisUsage(AU);
      AU.addRequired< ModuleLoops >();
      //AU.addRequired< DominatorTreeWrapperPass >();
      //AU.addRequired< PostDominatorTreeWrapperPass >();
      //AU.addRequired< LoopInfoWrapperPass >();
      AU.addRequired< TargetLibraryInfoWrapperPass >();
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

    ModRefResult modref(const Instruction *i1, TemporalRelation Rel,
                        const Value *p2, unsigned sz2, const Loop *L,
                        Remedies &R) {
      return LoopAA::modref(i1,Rel,p2,sz2,L,R);
    }

    ModRefResult modref(const Instruction *i1, TemporalRelation Rel,
                        const Instruction *i2, const Loop *L, Remedies &R);

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

    /// Determine if the block MUST KILL the specified pointer.
    /// If <after> belongs to this block and <after> is not null, only consider operations AFTER <after>
    /// If <after> belongs to this block and <before> is is not null, only consider operations BEFORE <before>
    bool blockMustKill(const BasicBlock *bb, const Value *ptr, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout, const Loop *L = nullptr);

    /// Determine if this instruction MUST KILL the specified pointer.
    bool instMustKill(const Instruction *inst, const Value *ptr, time_t queryStart, unsigned Timeout, const Loop *L = nullptr);

    const PostDominatorTree *getPDT(const Function *cf);
    const DominatorTree *getDT(const Function *cf);
    ScalarEvolution *getSE(const Function *cf);
    LoopInfo *getLI(const Function *cf);
  };

}

#endif // LLVM_LIBERTY_KILL_FLOW_H

