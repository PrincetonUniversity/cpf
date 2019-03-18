#define DEBUG_TYPE   "remediator"

#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"

namespace liberty
{
  using namespace llvm;

  Remedies Remediator::satisfy(const PDG &pdg, Loop *loop,
                               const Criticisms &criticisms) {
    Remedies remedies;
    for (Criticism *cr : criticisms) {
      Instruction *sop = dyn_cast<Instruction>(cr->getOutgoingT());
      Instruction *dop = dyn_cast<Instruction>(cr->getIncomingT());
      assert(sop && dop &&
             "PDG nodes that are part of criticims should be instructions");
      bool lc = cr->isLoopCarriedDependence();
      bool raw = cr->isRAWDependence();
      Remedy_ptr r;
      if (cr->isMemoryDependence())
        r = tryRemoveMemEdge(sop, dop, lc, raw, loop);
      else if (cr->isControlDependence())
        r = tryRemoveCtrlEdge(sop, dop, lc, loop);
      else
        r = tryRemoveRegEdge(sop, dop, lc, loop);
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
                                          bool raw, const Loop *loop) {
    RemedResp remedResp = memdep(sop, dop, lc, raw, loop);
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
                                           bool raw, const Loop *loop) {
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

  bool Remediator::noMemoryDep(const Instruction *src, const Instruction *dst,
                               LoopAA::TemporalRelation FW,
                               LoopAA::TemporalRelation RV, const Loop *loop,
                               LoopAA *aa, bool rawDep) {
    // forward dep test
    LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop);
    if (LoopAA::NoModRef == forward)
      return true;

    // forward is Mod, ModRef, or Ref

    // reverse dep test
    LoopAA::ModRefResult reverse = forward;

    if (src != dst)
      reverse = aa->modref(dst, RV, src, loop);

    if (LoopAA::NoModRef == reverse)
      return true;

    if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
      return true; // RaR dep; who cares.

    // At this point, we know there is one or more of
    // a flow-, anti-, or output-dependence.

    bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);
    bool WAR = (forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);
    bool WAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);

    if (rawDep && !RAW)
      return true;

    if (!rawDep && !WAR && !WAW)
      return true;

    return false;
  }

} // namespace liberty
