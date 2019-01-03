#define DEBUG_TYPE "txio-remed"

#include "llvm/ADT/Statistic.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"

#include "liberty/SpecPriv/TXIORemed.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/CallSiteFactory.h"

#define DEFAULT_TXIO_REMED_COST 20

namespace liberty {
using namespace llvm;

STATISTIC(numTXIOQueries, "Number of mem queries asked to txio");
STATISTIC(numTXIONoMemDep, "Number of mem deps removed by txio");

void TXIORemedy::apply(PDG &pdg) {
  // TODO: transfer the code for application of txio here.
}

bool TXIORemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<TXIORemedy> txioRhs =
      std::static_pointer_cast<TXIORemedy>(rhs);
  return this->printI < txioRhs->printI;
}

bool TXIORemediator::isTXIOFcn(const Instruction *inst) {
  CallSite cs = getCallSite(inst);
  if (!cs.getInstruction())
    return false;

  Function *callee = cs.getCalledFunction();
  if (!callee)
    return false;

  if (callee->getName() == "vfprintf")
    return true;
  else if (callee->getName() == "vprintf")
    return true;
  else if (callee->getName() == "fprintf")
    return true;
  else if (callee->getName() == "printf")
    return true;
  else if (callee->getName() == "fputs")
    return true;
  else if (callee->getName() == "puts")
    return true;
  else if (callee->getName() == "fputc")
    return true;
  else if (callee->getName() == "putc")
    return true;
  else if (callee->getName() == "putchar")
    return true;

  return false;
}

Remediator::RemedResp TXIORemediator::memdep(const Instruction *A,
                                             const Instruction *B,
                                             const bool LoopCarried,
                                             const Loop *L) {

  ++numTXIOQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<TXIORemedy> remedy =
      std::shared_ptr<TXIORemedy>(new TXIORemedy());
  remedy->cost = DEFAULT_TXIO_REMED_COST;

  if (!LoopCarried) {
    remedResp.remedy = remedy;
    return remedResp;
  }

  if (isTXIOFcn(A)) {
    ++numTXIONoMemDep;
    remedResp.depRes = DepResult::NoDep;
    remedy->printI = A;
  }
  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
