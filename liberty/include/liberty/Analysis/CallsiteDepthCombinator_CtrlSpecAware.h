/* CallSiteDepthCombinator

   This is a refinement on the CallSiteCombinator based on a few observations:
    (1) The CallSiteCombinator would generate new queries, but those queries
        would lose their calling context.
    (2) I'm much more concerned with loop-carried deps than intra-iteration
        deps.  This does not concern itself with intra-iteration deps.
    (3) I'm much more concern with flow memory deps than anti or output deps.
        This ONLY worries about flow deps, and declines to answer for anti
        or output deps.

   What does it do?

   It repeatedly expands callsites to sets of memory operations whose effect on
   memory may escape the calling context.  It maintains a context (nest of
   callsites) for each operation.  It features a lazy iterator so that it
   may efficiently explore all operations within a subtree of the callgraph.
 */
#ifndef LLVM_LIBERTY_CALLSITE_DEPTH_COMBINATOR_AA_H
#define LLVM_LIBERTY_CALLSITE_DEPTH_COMBINATOR_AA_H

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Analysis/CallsiteSearch_CtrlSpecAware.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/KillFlow_CtrlSpecAware.h"
#include "liberty/Analysis/QueryCacheing.h"

namespace liberty
{
  using namespace llvm;

  class KillFlow_CtrlSpecAware;

  class CallsiteDepthCombinator_CtrlSpecAware : public ModulePass, public liberty::LoopAA
  {
    typedef DenseMap< IIKey, bool > IICache;

    IICache iiCache;

    bool isEligible(const Instruction *i) const;

    KillFlow_CtrlSpecAware *killflow;

  protected:
    virtual void uponStackChange() { iiCache.clear(); }

  public:
    static char ID;
    CallsiteDepthCombinator_CtrlSpecAware() : ModulePass(ID), iiCache() {}

    virtual bool runOnModule(Module &M);

    virtual SchedulingPreference getSchedulingPreference() const { return SchedulingPreference( Normal - 1 ); }
    StringRef getLoopAAName() const { return "callsite-depth-combinator-aa"; }

    void getAnalysisUsage(AnalysisUsage &AU) const;

    void setLoopOfInterest(ControlSpeculation *cs, Loop *L) {
      cs->setLoopOfInterest(L->getHeader());
      killflow->setLoopOfInterest(cs,L);
    }

    /// Determine if it is possible for a store
    /// 'src' to flow to a load 'dst' across
    /// the backedge of L.
    static bool mayFlowCrossIter(
      const CtxInst_CtrlSpecAware &src, const CtxInst_CtrlSpecAware &dst, const Loop *L, KillFlow_CtrlSpecAware &kill,
      time_t queryStart=0, unsigned Timeout=0);

    /// Determine if it is possible for a store
    /// in write to flow to a load in read.
    /// across the backedge of L.
    static bool mayFlowCrossIter(
      KillFlow_CtrlSpecAware &kill,
      const Instruction *src, const Instruction *dst, const Loop *L,
      const CtxInst_CtrlSpecAware &write, const CtxInst_CtrlSpecAware &read,
      time_t queryStart=0, unsigned Timeout=0);

    /// Determine if it is possible for a store
    /// in write to flow to a load in read.
    /// within the SAME iteration of L.
    static bool mayFlowIntraIter(
      KillFlow_CtrlSpecAware &kill,
      const Instruction *src,
      const Instruction *dst,
      const Loop *L,
      const CtxInst_CtrlSpecAware &write,
      const CtxInst_CtrlSpecAware &read);


    /// Determine if any flow is possible from
    /// any store in src to any load in dst
    /// across the backedge of L.  If allFlowsOut
    /// is null, stop after we find at least one.
    /// If not null, collect all such flows into
    /// that output parameter.  Returns true if
    /// a flow is possible, or false otherwise.
    static bool doFlowSearchCrossIter(
      const Instruction *src, const Instruction *dst, const Loop *L,
      KillFlow_CtrlSpecAware &kill, CCPairs *allFlowsOut = 0,
      time_t queryStart=0, unsigned Timeout=0);

    /// Like the previous, but accepts a pre-computed
    /// inst-search object over src.
    static bool doFlowSearchCrossIter(
      const Instruction *src, const Instruction *dst, const Loop *L,
      InstSearch_CtrlSpecAware &writes,
      KillFlow_CtrlSpecAware &kill, CCPairs *allFlowsOut = 0,
      time_t queryStart=0, unsigned Timeout=0);

    /// Like the previous, but accepts pre-computed
    /// inst-search objects over src,dst.
    static bool doFlowSearchCrossIter(
      const Instruction *src, const Instruction *dst, const Loop *L,
      InstSearch_CtrlSpecAware &writes, InstSearch_CtrlSpecAware &reads,
      KillFlow_CtrlSpecAware &kill, CCPairs *allFlowsOut = 0,
      time_t queryStart=0, unsigned Timeout=0);


    ModRefResult modref(const Instruction *inst1,
                        TemporalRelation Rel,
                        const Instruction *inst2,
                        const Loop *L);

    ModRefResult modref(const Instruction *i1,
                        TemporalRelation Rel,
                        const Value *p2,
                        unsigned s2,
                        const Loop *L);

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
  };
}

#endif

