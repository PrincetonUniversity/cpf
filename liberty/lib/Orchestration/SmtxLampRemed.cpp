#define DEBUG_TYPE "smtx-lamp-remed"

#include "liberty/Orchestration/SmtxLampRemed.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEFAULT_LAMP_REMED_COST 999

namespace liberty {

using namespace llvm;

STATISTIC(numQueries, "Num queries");
STATISTIC(numEligible, "Num eligible queries");
STATISTIC(numNoFlow, "Num no-flow results");
STATISTIC(numSmtxAA, "Num removed via SmtxAA + CAF");

void SmtxLampRemedy::apply(Task *task) {
  // TODO: transfer the code for application of smtxLamp here.
}

bool SmtxLampRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<SmtxLampRemedy> smtxRhs =
      std::static_pointer_cast<SmtxLampRemedy>(rhs);
  if (this->writeI == smtxRhs->writeI)
    return this->readI < smtxRhs->readI;
  return this->writeI < smtxRhs->writeI;
}

static cl::opt<unsigned>
    Threshhold("smtx-lamp-threshhold2", cl::init(0), cl::NotHidden,
               cl::desc("Maximum number of observed flows to report NoModRef"));

static bool isMemIntrinsic(const Instruction *inst) {
  return isa<MemIntrinsic>(inst);
}

static bool intrinsicMayRead(const Instruction *inst) {
  ImmutableCallSite cs(inst);
  StringRef name = cs.getCalledFunction()->getName();
  if (name == "llvm.memset.p0i8.i32" || name == "llvm.memset.p0i8.i64")
    return false;

  return true;
}

Remedies SmtxLampRemediator::satisfy(const PDG &pdg, Loop *loop,
                                     const Criticisms &criticisms) {

  const DataLayout &DL = loop->getHeader()->getModule()->getDataLayout();
  smtxaa = new SmtxAA(smtxMan);
  smtxaa->InitializeLoopAA(&proxy, DL);

  Remedies remedies = Remediator::satisfy(pdg, loop, criticisms);

  delete smtxaa;

  return remedies;
}

Remediator::RemedResp SmtxLampRemediator::memdep(const Instruction *A,
                                                  const Instruction *B,
                                                  bool LoopCarried, bool RAW,
                                                  const Loop *L) {
  ++numQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<SmtxLampRemedy> remedy =
      std::shared_ptr<SmtxLampRemedy>(new SmtxLampRemedy());
  remedy->cost = DEFAULT_LAMP_REMED_COST;

  // Lamp profile data is loop sensitive.
  if (!L) {
    // Inapplicable
    remedResp.remedy = remedy;
    return remedResp;
  }

  // sot: avoid intra-iter memory speculation with llamp. Presence of speculated
  // II complicates validation process, and potentially forces high false
  // positive rate of misspecs or extensive and regular checkpoints. Benefits
  // for parallelization have not proven to be significant. If further
  // experiments prove otherwise this change might be reverted. Note that
  // neither Hanjun nor Taewook used II mem spec for similar reasons.
  if (!LoopCarried) {
    remedResp.remedy = remedy;
    return remedResp;
  }

  //const DataLayout &DL = A->getModule()->getDataLayout();
  //smtxaa->InitializeLoopAA(&proxy, DL);
  // This AA stack includes static analysis and memory speculation
  LoopAA *aa = smtxaa->getTopAA();
  // aa->dump();


  // Lamp profile data is only collected for
  // loads and stores; not callsites.
  // Lamp collects FLOW and OUTPUT info, but
  // not ANTI or FALSE dependence data.
  // Thus, for Before/Same queries, we are looking
  // for Store -> Load/Store

  if ((!isa<StoreInst>(A) && !isMemIntrinsic(A)) ||
      (!isa<LoadInst>(B) && !(isMemIntrinsic(B) && intrinsicMayRead(B)))) {
    // Callsites, etc: inapplicable

    bool noDep =
        (LoopCarried)
            ? noMemoryDep(A, B, LoopAA::Before, LoopAA::After, L, aa, RAW)
            : noMemoryDep(A, B, LoopAA::Same, LoopAA::Same, L, aa, RAW);
    if (noDep) {
      ++numSmtxAA;
      remedResp.depRes = DepResult::NoDep;
    }

    remedResp.remedy = remedy;
    return remedResp;
  }

  ++numEligible;

  LAMPLoadProfile &lamp = smtxMan->getLampResult();

  remedy->writeI = A;
  remedy->readI = B;

  if (LoopCarried) {

    // Query profile data for a loop-carried flow from A to B
    if (lamp.numObsInterIterDep(L->getHeader(), B, A) <= Threshhold) {
      // No flow.
      ++numNoFlow;
      remedResp.depRes = DepResult::NoDep;

      DEBUG(errs() << "No observed InterIterDep between " << *A << "  and  "
                   << *B << "\n");

      // Keep track of this

      smtxMan->setAssumedLC(L, A, B);
      remedResp.remedy = remedy;
      return remedResp;
    }
  }

  /*
  else {
    // Query profile data for an intra-iteration flow from A to B
    if (lamp.numObsIntraIterDep(L->getHeader(), B, A) <= Threshhold) {
      // No flow
      ++numNoFlow;
      remedResp.depRes = DepResult::NoDep;

      DEBUG(errs() << "No observed IntraIter dep from " << *A << "  and  " << *B
                   << "\n");

      // Keep track of this

      // queryAcrossCallsites(A,Same,B,L);
      smtxMan->setAssumedII(L, A, B);
      remedResp.remedy = remedy;
      return remedResp;
    }
  }
  */

  // check if collaboration of AA and SmtxAA achieves better accuracy
  bool noDep =
      (LoopCarried)
          ? noMemoryDep(A, B, LoopAA::Before, LoopAA::After, L, aa, RAW)
          : noMemoryDep(A, B, LoopAA::Same, LoopAA::Same, L, aa, RAW);
  if (noDep) {
    ++numSmtxAA;
    remedResp.depRes = DepResult::NoDep;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
