#ifndef LIBERTY_SPEC_PRIV_PTR_RESIDUE_SPECULATION_MANAGER_H
#define LIBERTY_SPEC_PRIV_PTR_RESIDUE_SPECULATION_MANAGER_H

#include "llvm/Pass.h"
#include "liberty/SpecPriv/Read.h"
#include "liberty/SpecPriv/UpdateOnClone.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

/// Tracks pointer residues that contribute to dependence analysis.
struct PtrResidueSpeculationManager : public ModulePass, public UpdateOnClone
{
  static char ID;
  PtrResidueSpeculationManager() : ModulePass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &au) const;
  virtual StringRef getPassName() const { return "Pointer Residue speculation manager"; }
  virtual bool runOnModule(Module &mod);

  typedef std::pair<const Value*,const Ctx*> Assumption;

  const Read &getSpecPrivResult() const { return *spresults; }
  bool isAssumed(const Assumption &) const;
  void setAssumed(const Assumption &);

  typedef std::set<Assumption> Assumptions;
  typedef Assumptions::const_iterator iterator;

  /// Iterate over all assumptions made during analysis.
  iterator begin() const { return assumptions.begin(); }
  iterator end() const { return assumptions.end(); }

  // update on clone
  virtual void contextRenamedViaClone(
    const Ctx *changed,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap);

  void reset() { assumptions.clear(); }

private:
  const Read *spresults;
  Assumptions assumptions;
};

}
}
#endif
