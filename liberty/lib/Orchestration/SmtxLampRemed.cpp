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

void SmtxLampRemedy::apply(PDG &pdg) {
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

  // Lamp profile data is only collected for
  // loads and stores; not callsites.
  // Lamp collects FLOW and OUTPUT info, but
  // not ANTI or FALSE dependence data.
  // Thus, for Before/Same queries, we are looking
  // for Store -> Load/Store

  if (!isa<StoreInst>(A) && !isMemIntrinsic(A)) {
    // Callsites, etc: inapplicable
    remedResp.remedy = remedy;
    return remedResp;
  }

  // Again, only Store vs (Load/Store)
  if (!isa<LoadInst>(B) && !(isMemIntrinsic(B) && intrinsicMayRead(B))) {
    // inapplicable
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
    }
  }

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
    }
  }
  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
