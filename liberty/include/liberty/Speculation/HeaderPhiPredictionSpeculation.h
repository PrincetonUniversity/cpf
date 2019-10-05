#ifndef LIBERTY_SPEC_PRIV_HEADER_PHI_PREDICTION_SPECULATION_STACK_H
#define LIBERTY_SPEC_PRIV_HEADER_PHI_PREDICTION_SPECULATION_STACK_H

#include "llvm/IR/DataLayout.h"

#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/SLAMP/SlampOracleAA.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Speculation/ControlSpeculator.h"

//#include "SmtxAA.h"

#include <map>
#include <vector>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct HeaderPhiPredictionSpeculation : public ModulePass, public PredictionSpeculation
{
  static char ID;
  HeaderPhiPredictionSpeculation() : ModulePass(ID) {}
  ~HeaderPhiPredictionSpeculation();

  StringRef getPassName() const { return "HeaderPhiPredictionSpeculation"; }

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &m);

  virtual bool isPredictable(const Instruction *I, const Loop *l);

  typedef std::pair<PHINode*, LoadInst*>          PhiLoadPair;
  typedef std::multimap<BasicBlock*, PhiLoadPair> Loop2PhiLoadPair;
  typedef Loop2PhiLoadPair::const_iterator        pair_iterator;

  typedef std::multimap<BasicBlock*, PHINode*> Loop2Phi;
  typedef Loop2Phi::const_iterator             phi_iterator;

  pair_iterator pair_begin(const Loop* loop) const;
  pair_iterator pair_end(const Loop* loop) const;

  phi_iterator phi_begin(const Loop* loop) const;
  phi_iterator phi_end(const Loop* loop) const;

  phi_iterator spec_phi_begin(const Loop* loop) const;
  phi_iterator spec_phi_end(const Loop* loop) const;

private:
  void buildSpeculativeAnalysisStack();

  void collectDefs(LoadInst* li, Loop* loop, std::vector<Instruction*>& srcs);

  Instruction* getBiasedIncoming(PHINode* phi);

  bool queryIntraIterationMemoryDep(Instruction* sop, Instruction* dop, Loop* loop);
  bool queryLoopCarriedMemoryDep(Instruction* sop, Instruction* dop, Loop* loop);
  bool queryMemoryDep(Instruction* sop, Instruction* dop,
      LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV, Loop* loop);
  LoopAA::ModRefResult query(Instruction *sop,
      LoopAA::TemporalRelation rel,
      Instruction *dop,
      Loop *loop, Remedies &R);

  const DataLayout*                     td;
  ProfileGuidedControlSpeculator* ctrlspec;
  //SmtxAA*                         smtxaa;
  SlampOracle*                    slampaa;
  EdgeCountOracle*                edgeaa;
  LoopAA*                         aa;
  ModuleLoops*                    mloops;

  typedef std::pair<Instruction*, Instruction*> DepEdge;
  typedef std::map<BasicBlock*, std::map<DepEdge, bool > > DependenceCache;

  DependenceCache cache;

  Loop2PhiLoadPair predicted_pairs;
  Loop2Phi         predicted_phis;
  Loop2Phi         speculated_phis;

};

}
}

#endif
