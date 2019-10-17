#define DEBUG_TYPE "mem-ver-remed"

#include "llvm/ADT/Statistic.h"

#include "liberty/Orchestration/MemVerRemed.h"

//#define DEFAULT_MEM_VER_REMED_COST 49
#define DEFAULT_MEM_VER_REMED_COST 151
#define WAR_MEM_VER_REMED_COST 25

namespace liberty {
using namespace llvm;

STATISTIC(numNoMemDep,
          "Number of false mem deps removed with memory versioning");

void MemVerRemedy::apply(Task *task) {
  // TODO: code for application of memory versioning here.
  // doing process-based parallelization suffices
}

bool MemVerRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<MemVerRemedy> memVerRhs =
      std::static_pointer_cast<MemVerRemedy>(rhs);
  return this->waw < memVerRhs->waw;
}

Remediator::RemedResp MemVerRemediator::memdep(const Instruction *A,
                                               const Instruction *B,
                                               bool LoopCarried,
                                               DataDepType dataDepTy,
                                               const Loop *L) {

  Remediator::RemedResp remedResp;

  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<MemVerRemedy> remedy =
      std::shared_ptr<MemVerRemedy>(new MemVerRemedy());
  remedy->cost = DEFAULT_MEM_VER_REMED_COST;
  bool RAW = dataDepTy == DataDepType::RAW;
  bool WAW = dataDepTy == DataDepType::WAW;

  // need to be loop-carried WAW or WAR
  if (LoopCarried && !RAW) {
    ++numNoMemDep;
    remedResp.depRes = DepResult::NoDep;
    if (WAW) {
      remedy->waw = true;
      // DISABLE WAW functionality for MemVer (Cannot handle
      // last-liveout value in the codegen)
      remedResp.depRes = DepResult::Dep;
    }
    else {
      // with process-based parallelization, WAR are removed for free.
      // WAW dep removal is expensive since the last write to each mem
      // location needs to be tracked
      remedy->waw = false;
      remedy->cost = WAR_MEM_VER_REMED_COST;
    }
    DEBUG(errs() << "MemVerRemed removed false mem dep between inst " << *A
                 << "  and  " << *B << '\n');
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
