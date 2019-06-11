#define DEBUG_TYPE "mem-ver-remed"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/MemVerRemed.h"

//#define DEFAULT_MEM_VER_REMED_COST 49
#define DEFAULT_MEM_VER_REMED_COST 151

namespace liberty {
using namespace llvm;

STATISTIC(numNoMemDep,
          "Number of false mem deps removed with memory versioning");

void MemVerRemedy::apply(Task *task) {
  // TODO: code for application of memory versioning here.
  // doing process-based parallelization suffices
}

bool MemVerRemedy::compare(const Remedy_ptr rhs) const {
  // only one remedy is produced by this remediator
  return false;
}

Remediator::RemedResp MemVerRemediator::memdep(const Instruction *A,
                                               const Instruction *B,
                                               bool LoopCarried, bool RAW,
                                               const Loop *L) {

  Remediator::RemedResp remedResp;

  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<MemVerRemedy> remedy =
      std::shared_ptr<MemVerRemedy>(new MemVerRemedy());
  remedy->cost = DEFAULT_MEM_VER_REMED_COST;

  // need to be loop-carried WAW or WAR
  if (LoopCarried && !RAW) {
    ++numNoMemDep;
    remedResp.depRes = DepResult::NoDep;
    DEBUG(errs() << "MemVerRemed removed false mem dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
