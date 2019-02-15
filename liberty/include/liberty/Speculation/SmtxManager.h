#ifndef LIBERTY_SPEC_PRIV_SMTX_SPECULATION_MANAGER_H
#define LIBERTY_SPEC_PRIV_SMTX_SPECULATION_MANAGER_H

#include "llvm/Pass.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/Speculation/UpdateOnClone.h"
#include "liberty/Strategy/PipelineStrategy.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

/// Tracks lamp results that contribute to dependence analysis.
struct SmtxSpeculationManager : public ModulePass, public UpdateOnClone
{
  static char ID;
  SmtxSpeculationManager() : ModulePass(ID), UpdateOnClone() {}

  virtual void getAnalysisUsage(AnalysisUsage &au) const;
  virtual StringRef getPassName() const { return "SMTX speculation manager"; }
  virtual bool runOnModule(Module &mod);

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

  // Add to the set of assumptions
  void setAssumedLC(const Loop *loop, const Instruction *src, const Instruction *dst);
  void setAssumedII(const Loop *loop, const Instruction *stc, const Instruction *dst);

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

  LAMPLoadProfile &getLampResult() const { return *lampResult; }

  void reset();

private:
  LAMPLoadProfile *lampResult;
  Loop2Assumptions  iiNoFlow;
  Loop2Assumptions  lcNoFlow;

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
};

}
}
#endif
