// Decides which values to perform value speculation on,
// and manages as list of speculated values.
// Also, contains an adaptor to the LoopAA stack
#ifndef LIBERTY_SPEC_PRIV_PREDICTION_ORACLE_AA_H
#define LIBERTY_SPEC_PRIV_PREDICTION_ORACLE_AA_H

#include "scaf/SpeculationModules/PredictionSpeculation.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/UpdateOnClone.h"

#include <map>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct ProfileGuidedPredictionSpeculator : public ModulePass, public PredictionSpeculation, public UpdateOnClone
{
  static char ID;
  ProfileGuidedPredictionSpeculator() : ModulePass(ID) {}

  StringRef getPassName() const { return "Value-prediction Speculation Manager"; }

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &mod) { return false; }

  virtual bool isPredictable(const Instruction *I, const Loop *loop);

  typedef std::multimap<BasicBlock*, const LoadInst*> Loop2Load;
  typedef Loop2Load::const_iterator load_iterator;

  load_iterator loads_begin(const Loop *loop) const;
  load_iterator loads_end(const Loop *loop) const;

  // Update on clone
  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap);

  void reset() { predictedLoads.clear(); }

private:
  Loop2Load predictedLoads;
};

}
}

#endif

