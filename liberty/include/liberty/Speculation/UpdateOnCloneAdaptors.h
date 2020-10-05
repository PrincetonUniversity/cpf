#ifndef LLVM_LIBERTY_SPEC_PRIV_UPATE_ON_CLONE_ADAPTORS_H
#define LLVM_LIBERTY_SPEC_PRIV_UPATE_ON_CLONE_ADAPTORS_H

#include "scaf/Utilities/InlineFunctionWithVmap.h"
#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"

namespace liberty
{
namespace SpecPriv
{

//sot: UpdateEdgeLoopProfilers seems not to be used. delete for now since it contains a lot of ProfileInfo code (deprecated code), need to change to BranchProbabilityInfo and BlockFrequencyInfo

struct UpdateEdgeLoopProfilers
{
  UpdateEdgeLoopProfilers(
    Pass& proxy,
    //ProfileInfo &ecp, LoopProfLoad &lpl) : edges(ecp), times(lpl) {}
    //BranchProbabilityInfo &bpi,
    //BlockFrequencyInfo &bfi,
    LoopProfLoad &lpl) : proxy(proxy), //bpi(bpi), bfi(bfi),
    times(lpl) {}

  void resetAfterInline(
    Instruction *callsite_no_longer_exists,
    Function *caller,
    Function *callee,
    const ValueToValueMapTy &vmap,
    const CallsPromotedToInvoke &call2invoke);

private:
  Pass &proxy;
  //ProfileInfo edges;
  LoopProfLoad &times;

  // sot remove this sanity check for now. uses deprecated ProfileInfo
  //void sanity(StringRef time, const Function *fcn) const;
};


struct UpdateLAMP
{
  UpdateLAMP(LAMPLoadProfile &llp) : prof(llp) {}

  void resetAfterInline(
    Instruction *callsite_no_longer_exists,
    Function *caller,
    Function *callee,
    const ValueToValueMapTy &vmap,
    const CallsPromotedToInvoke &call2invoke);

private:
  LAMPLoadProfile &prof;
};

}
}

#endif
