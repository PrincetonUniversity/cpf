#ifndef LLVM_LIBERTY_MEM_SPEC_AA_REMED_H
#define LLVM_LIBERTY_MEM_SPEC_AA_REMED_H

#include "llvm/IR/DataLayout.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/QueryCacheing.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LAMP/LampOracleAA.h"
#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Orchestration/PointsToAA.h"
#include "liberty/Orchestration/LocalityAA.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"

namespace liberty {
using namespace llvm;

class MemSpecAARemedy : public Remedy {
public:
  const Instruction *srcI;
  const Instruction *dstI;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "mem-spec-aa-remedy"; };
};

class MemSpecAARemediator : public Remediator {
public:
  MemSpecAARemediator(Pass &p, ControlSpeculation *cs, LAMPLoadProfile *lp,
                      const Read &read, const HeapAssignment &c,
                      PredictionSpeculation *ps)
      : Remediator(), proxy(p), ctrlspec(cs), lamp(lp), spresults(read),
        asgn(c), predspec(ps) {}

  StringRef getRemediatorName() const { return "mem-spec-aa-remediator"; }

  Remedies satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms);

  RemedResp memdep(const Instruction *A, const Instruction *B, bool LoopCarried,
                   bool RAW, const Loop *L);

private:
  Pass &proxy;
  ControlSpeculation *ctrlspec;
  EdgeCountOracle *edgeaa;
  LAMPLoadProfile *lamp;
  LampOracle *lampaa;
  const Read &spresults;
  PointsToAA *pointstoaa;
  const HeapAssignment &asgn;
  LocalityAA *localityaa;
  PredictionSpeculation *predspec;
  PredictionAA *predaa;
};

} // namespace liberty

#endif
