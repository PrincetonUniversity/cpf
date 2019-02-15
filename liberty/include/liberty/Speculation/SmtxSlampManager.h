#ifndef LIBERTY_SPEC_PRIV_SMTX_SLAMP_SPECULATION_MANAGER_H
#define LIBERTY_SPEC_PRIV_SMTX_SLAMP_SPECULATION_MANAGER_H

#include "llvm/Pass.h"
#include "liberty/SLAMP/SLAMPLoad.h"
#include "liberty/Speculation/UpdateOnClone.h"
#include "liberty/Strategy/PipelineStrategy.h"

namespace liberty
{
namespace SpecPriv
{

using namespace llvm;
using namespace slamp;

/// Tracks lamp results that contribute to dependence analysis.
struct SmtxSlampSpeculationManager : public ModulePass, public UpdateOnClone
{
  static char ID;
  SmtxSlampSpeculationManager() : ModulePass(ID), UpdateOnClone() {}

  virtual void getAnalysisUsage(AnalysisUsage &au) const;
  virtual StringRef getPassName() const { return "SLAMP-based SMTX speculation manager"; }
  virtual bool runOnModule(Module &mod);

  struct LinearPredictor
  {
    const unsigned context;
    const int64_t  a;
    const int64_t  b;
    const bool     is_double;

    LinearPredictor(const unsigned c, const int64_t a, const int64_t b, const bool d)
    : context(c), a(a), b(b), is_double(d) {}

    bool operator<(const LinearPredictor &other) const;
    bool operator==(const LinearPredictor &other) const;
    bool operator!=(const LinearPredictor &other) const;
  };

  typedef std::set<LinearPredictor> LinearPredictors;
  typedef std::map<const LoadInst*, LinearPredictors> Load2LPs;
  typedef std::map<const BasicBlock*, Load2LPs> Loop2Load2LPs;

  typedef std::map<const CallInst*, unsigned> Contexts;
  typedef std::map<const BasicBlock*, Contexts> Loop2Contexts;

  typedef std::map<const LoadInst*, std::set<unsigned> > Load2ContextIDs;
  typedef std::map<const BasicBlock*, Load2ContextIDs>   Loop2Load2ContextIDs;

  struct Assumption
  {
    Assumption(const Instruction *s, const Instruction *d)
      : src(s), dst(d) {}

    const Instruction *src;
    const Instruction *dst;

    bool operator<(const Assumption &other) const;
    bool operator==(const Assumption &other) const;
    bool operator!=(const Assumption &other) const;
  };

  typedef std::vector<Assumption> Assumptions;
  typedef std::map<const BasicBlock*, Assumptions> Loop2Assumptions;

  typedef Assumptions::const_iterator iterator;

  typedef std::map<Function*, set<StoreInst*> > Fcn2Writes;
  typedef std::map<Function*, set<LoadInst*> >  Fcn2Reads;


  /// Iterate over all assumptions made during analysis.
  iterator begin_lc(const Loop *loop) const;
  iterator end_lc(const Loop *loop) const;

  iterator begin_ii(const Loop *loop) const;
  iterator end_ii(const Loop *loop) const;

  // Determine whether a given assumption has already been made
  bool isAssumedLC(const Loop *loop, const Instruction *src, const Instruction *dst) const;
  bool isAssumedII(const Loop *loop, const Instruction *src, const Instruction *dst) const;

  // Does 'dst' sink a loop-carried or intra-iteration dependence w.r.t. 'loop'?
  bool sourcesSpeculativelyRemovedEdge(const Loop *loop, const Instruction *src) const;
  bool sinksSpeculativelyRemovedEdge(const Loop *loop, const Instruction *dst) const;

  // Does 'dst' use loop-invariant prediction or linear predictor?
  bool useLoopInvariantPrediction(Loop* loop, const Instruction* dst) const;
  bool useLinearPredictor(Loop* loop, const Instruction* dst) const;

  // Add to the set of assumptions
  void setAssumedLC(const Loop *loop, const Instruction *src, const Instruction *dst,
    const Instruction* context = NULL);
  void setAssumedLC(const Loop *loop, const Instruction *src, const LoadInst *dst,
    const Instruction* context, int64_t a, int64_t b, bool is_double);
  void setAssumedII(const Loop *loop, const Instruction *stc, const Instruction *dst,
    const Instruction* context = NULL);
  void setAssumedII(const Loop *loop, const Instruction *stc, const LoadInst *dst,
    const Instruction* context, int64_t a, int64_t b, bool is_double);

  std::set<unsigned> getLIPredictionApplicableCtxts(Loop* loop, LoadInst* dst);
  LinearPredictors&  getLinearPredictors(Loop* loop, const LoadInst* dst);

#if 0
  Contexts& getContexts(Loop* loop) { return all_ctxts[loop->getHeader()]; }
#endif
  Contexts& getContexts() { return unique_ctxts; }

  // Perform what Neil calls 'unspeculation' with respect to a given pipeline.
  // The first round of analysis will identify those edges which
  // can be speculated at a low misspec rate; we want to identify the subset of those
  // edges which are necessary to achieve the given pipeline.  In particular:
  //  - (anti-pipeline) keep those edges from a later stage to an earlier stage.
  //  - (anti-parallel stage) keep those loop-carried edges to/from a parallel stage.
  void unspeculate(const Loop *loop, const PipelineStrategy &pipeline);

  // update on clone
  virtual void contextRenamedViaClone(
    const Ctx *changed,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap);

  SLAMPLoadProfile &getSlampResult() const { return *slampResult; }

  void reset();

  void collectWrites(Function* fcn, vector<const Instruction*>& writes);
  void collectReads(Function* fcn, vector<const Instruction*>& reads);

private:
  SLAMPLoadProfile* slampResult;
  Loop2Assumptions  iiNoFlow;
  Loop2Assumptions  lcNoFlow;

  Contexts               unique_ctxts;
  Load2ContextIDs        ctxts_per_load;
  Load2LPs               load2lps;

#if 0
  Loop2Contexts          all_ctxts;
  Loop2Load2ContextIDs   ctxts_per_load;
  Loop2Load2LPs          loop2load2lps;
#endif

  Fcn2Writes        fcn2writes;
  Fcn2Reads         fcn2reads;

  const Assumptions &getLC(const Loop *loop) const;
  const Assumptions &getII(const Loop *loop) const;

  Assumptions &getLC(const Loop *loop);
  Assumptions &getII(const Loop *loop);

  // Does 'src' source a dependence?
  bool sourcesSpeculativelyRemovedEdge(
    const Loop *loop,
    const Loop2Assumptions &l2a,
    const Instruction *src) const;

  // Does 'dst' sink a dependence?
  bool sinksSpeculativelyRemovedEdge(
    const Loop *loop,
    const Loop2Assumptions &l2a,
    const Instruction *dst) const;

  unsigned registerContext(const Loop* loop, const CallInst* callinst);

  void addLoopInvariantPredictableContext(
    const Loop* loop,
    const LoadInst* load,
    unsigned ctxt_id);

  void sweep(Function::iterator begin, Function::iterator end, set<BasicBlock*>& bbs);
  void sweep(BasicBlock* bb, set<BasicBlock*>& bbs);
  void sweep(Function* fcn, set<BasicBlock*>& bbs);
};

}
}
#endif
