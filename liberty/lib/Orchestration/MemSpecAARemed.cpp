#define DEBUG_TYPE "mem-spec-aa-remed"

#include "liberty/Orchestration/MemSpecAARemed.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEFAULT_MEM_SPEC_AA_REMED_COST 1500

namespace liberty {

using namespace llvm;

STATISTIC(numQueries, "Num queries");
STATISTIC(numNoFlow, "Num no-flow results");

void MemSpecAARemedy::apply(Task *task) {
  // TODO: transfer the code for application of mem-spec-aa here.
}

bool MemSpecAARemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<MemSpecAARemedy> memSpecAARhs =
      std::static_pointer_cast<MemSpecAARemedy>(rhs);
  if (this->srcI == memSpecAARhs->srcI)
    return this->dstI < memSpecAARhs->dstI;
  return this->srcI < memSpecAARhs->srcI;
}

Remedies MemSpecAARemediator::satisfy(const PDG &pdg, Loop *loop,
                                      const Criticisms &criticisms) {

  const DataLayout &DL = loop->getHeader()->getModule()->getDataLayout();

  // CtrlSpec
  edgeaa = new EdgeCountOracle(ctrlspec);
  edgeaa->InitializeLoopAA(&proxy, DL);

  // LAMP
  lampaa = new LampOracle(lamp);
  lampaa->InitializeLoopAA(&proxy, DL);

  // Points-to
  pointstoaa = new PointsToAA(spresults);
  pointstoaa->InitializeLoopAA(&proxy, DL);

  // Separation Spec
  const Ctx *ctx = spresults.getCtx(loop);
  localityaa = new LocalityAA(spresults, asgn, ctx);
  localityaa->InitializeLoopAA(&proxy, DL);

  // Value prediction
  predaa = new PredictionAA(predspec);
  predaa->InitializeLoopAA(&proxy, DL);

  Remedies remedies = Remediator::satisfy(pdg, loop, criticisms);

  // remove these four AAs from the stack by destroying them
  delete edgeaa;
  delete lampaa;
  delete pointstoaa;
  delete localityaa;
  delete predaa;

  return remedies;
}

Remediator::RemedResp MemSpecAARemediator::memdep(const Instruction *A,
                                                  const Instruction *B,
                                                  bool LoopCarried, bool RAW,
                                                  const Loop *L) {
  ++numQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<MemSpecAARemedy> remedy =
      std::shared_ptr<MemSpecAARemedy>(new MemSpecAARemedy());
  remedy->cost = DEFAULT_MEM_SPEC_AA_REMED_COST;
  remedy->srcI = A;
  remedy->dstI = B;

  // avoid intra-iter memory speculation. Presence of speculated II complicates
  // validation process, and potentially forces high false positive rate of
  // misspecs or extensive and regular checkpoints. Benefits for parallelization
  // have not proven to be significant in preliminary experiments. If further
  // experiments prove otherwise this change might be reverted. Note that
  // neither Hanjun nor Taewook used II mem spec for similar reasons.
  if (!LoopCarried) {
    remedResp.remedy = remedy;
    return remedResp;
  }

  // This AA stack includes static analysis, flow dependence speculation,
  // locality, value prediction and control speculation.
  LoopAA *aa = predaa->getTopAA();
  //aa->dump();

  bool noDep = noMemoryDep(A, B, LoopAA::Before, LoopAA::After, L, aa, RAW);
  if (noDep) {
    ++numNoFlow;
    remedResp.depRes = DepResult::NoDep;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
