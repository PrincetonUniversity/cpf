#define DEBUG_TYPE   "remediator"

#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/SpecPriv/Remediator.h"

namespace liberty
{
  using namespace llvm;

  Remedies Remediator::satisfy(const PDG &pdg, const Criticisms &criticisms) {
    Remedies remedies;
    for (Criticism cr : criticisms) {
      Vertices::ID src, dst;
      bool lc;
      DepType dt;
      std::tie(src, dst, lc, dt) = cr;
      const Vertices &V = pdg.getV();
      Loop *loop = V.getLoop();
      Instruction *sop = V.get(src);
      Instruction *dop = V.get(dst);
      Remedy_ptr r;
      if (dt == DepType::Mem)
        r = tryRemoveMemEdge(sop, dop, lc, loop);
      else if (dt == DepType::Reg)
        r = tryRemoveRegEdge(sop, dop, lc, loop);
      else
        r = tryRemoveCtrlEdge(sop, dop, lc, loop);
      if (r) {
        // remedy found for this criticism
        auto it = remedies.find(r);
        if (it != remedies.end()) {
          // this remedy already satisfied previous criticim(s)
          (*it)->resolvedC.insert(cr);
        }
        else {
          r->resolvedC.insert(cr);
          remedies.insert(r);
        }
      }
    }
    return remedies;
  }

  Remedy_ptr Remediator::tryRemoveMemEdge(const Instruction *sop,
                                          const Instruction *dop, bool lc,
                                          const Loop *loop) {
    RemedResp remedResp = memdep(sop, dop, lc, loop);
    if (remedResp.depRes == DepResult::NoDep)
      return remedResp.remedy;
    else
      return nullptr;
  }

  Remedy_ptr Remediator::tryRemoveRegEdge(const Instruction *sop,
                                          const Instruction *dop, bool lc,
                                          const Loop *loop) {
    RemedResp remedResp = regdep(sop, dop, lc, loop);
    if (remedResp.depRes == DepResult::NoDep)
      return remedResp.remedy;
    else
      return nullptr;
  }

  Remedy_ptr Remediator::tryRemoveCtrlEdge(const Instruction *sop,
                                           const Instruction *dop, bool lc,
                                           const Loop *loop) {
    RemedResp remedResp = ctrldep(sop, dop, loop);
    if (remedResp.depRes == DepResult::NoDep)
      return remedResp.remedy;
    else
      return nullptr;
  }

  // default conservative implementation of memdep,regdep,ctrldep

  Remediator::RemedResp Remediator::memdep(const Instruction *sop,
                                           const Instruction *dop, bool lc,
                                           const Loop *loop) {
    RemedResp remedResp;
    remedResp.depRes = DepResult::Dep;
    return remedResp;
  }

  Remediator::RemedResp Remediator::regdep(const Instruction *sop,
                                           const Instruction *dop, bool lc,
                                           const Loop *loop) {
    RemedResp remedResp;
    remedResp.depRes = DepResult::Dep;
    return remedResp;
  }

  Remediator::RemedResp Remediator::ctrldep(const Instruction *sop,
                                            const Instruction *dop,
                                            const Loop *loop) {
    RemedResp remedResp;
    remedResp.depRes = DepResult::Dep;
    return remedResp;
  }

} // namespace liberty
