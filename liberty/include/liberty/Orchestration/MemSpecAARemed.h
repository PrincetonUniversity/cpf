#ifndef LLVM_LIBERTY_MEM_SPEC_AA_REMED_H
#define LLVM_LIBERTY_MEM_SPEC_AA_REMED_H

#include "llvm/IR/DataLayout.h"

#include "scaf/MemoryAnalysisModules/KillFlow.h"
#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/MemoryAnalysisModules/QueryCacheing.h"
#include "scaf/MemoryAnalysisModules/SimpleAA.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LAMP/LampOracleAA.h"
#include "liberty/Orchestration/CommutativeLibsAA.h"
#include "liberty/Orchestration/EdgeCountOracleAA.h"
#include "liberty/Orchestration/LocalityAA.h"
#include "liberty/Orchestration/PointsToAA.h"
#include "liberty/Orchestration/PrivAA.h"
#include "liberty/Orchestration/PtrResidueAA.h"
#include "liberty/Orchestration/ReadOnlyAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Orchestration/ShortLivedAA.h"
#include "liberty/Orchestration/SmtxAA.h"
#include "liberty/Orchestration/TXIOAA.h"
#include "liberty/Speculation/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;

class MemSpecAARemedy : public Remedy {
public:
  const Instruction *srcI;
  const Instruction *dstI;

  Remedies subR;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "mem-spec-aa-remedy"; };

  bool hasSubRemedies() { return true; }
  Remedies *getSubRemedies() { return &subR; }
};

class MemSpecAARemediator : public Remediator {
public:
  MemSpecAARemediator(Pass &p, ControlSpeculation *cs, LAMPLoadProfile *lp,
                      const Read &read, const HeapAssignment &c,
                      PredictionSpeculation *ps,
                      SpecPriv::SmtxSpeculationManager *sman,
                      PtrResidueSpeculationManager *pman,
                      KillFlow_CtrlSpecAware *killflowA,
                      CallsiteDepthCombinator_CtrlSpecAware *callsiteA,
                      KillFlow &kill, ModuleLoops &ml, PerformanceEstimator *pf)
      : Remediator(), proxy(p), ctrlspec(cs), lamp(lp), spresults(read),
        asgn(c), predspec(ps), smtxMan(sman), ptrresMan(pman),
        killflow_aware(killflowA), callsite_aware(callsiteA), killFlow(kill),
        mloops(ml), perf(pf) {}

  StringRef getRemediatorName() const { return "mem-spec-aa-remediator"; }

  Remedies satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms);

  RemedResp memdep(const Instruction *A, const Instruction *B, bool LoopCarried,
                   DataDepType dataDepTy, const Loop *L);

private:
  Pass &proxy;
  ControlSpeculation *ctrlspec;
  EdgeCountOracle *edgeaa;
  LAMPLoadProfile *lamp;
  // LampOracle *lampaa;
  SmtxAA *smtxaa;
  const Read &spresults;
  PointsToAA *pointstoaa;
  const HeapAssignment &asgn;
  LocalityAA *localityaa;
  PredictionSpeculation *predspec;
  SpecPriv::SmtxSpeculationManager *smtxMan;
  PredictionAA *predaa;
  PtrResidueAA *ptrresaa;
  PtrResidueSpeculationManager *ptrresMan;
  ReadOnlyAA *roaa;
  ShortLivedAA *localaa;
  TXIOAA *txioaa;
  CommutativeLibsAA *commlibsaa;
  SimpleAA *simpleaa;
  KillFlow_CtrlSpecAware *killflow_aware;
  CallsiteDepthCombinator_CtrlSpecAware *callsite_aware;
  KillFlow &killFlow;
  ModuleLoops &mloops;
  PrivAA *privaa;
  PerformanceEstimator *perf;
};

} // namespace liberty

#endif
