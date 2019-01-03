#ifndef LLVM_LIBERTY_SPEC_PRIV_UPATE_ON_CLONE_H
#define LLVM_LIBERTY_SPEC_PRIV_UPATE_ON_CLONE_H

#include "llvm/Transforms/Utils/Cloning.h"

#include "liberty/SpecPriv/Pieces.h"
#include "liberty/SpecPriv/FoldManager.h"

#include <vector>

namespace liberty
{
namespace SpecPriv
{


// This is an interface which allows analysis to
// be updated when I clone and specialize functions.
struct UpdateOnClone
{
  virtual ~UpdateOnClone() {}

  /// Causes data structures to adjust their results
  /// accoding to a context rename.  Specificially,
  /// a clone of one or more functions was created so
  /// that two different calling contexts of the
  /// functions can be statically disambiguated.
  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap) = 0;
};

struct UpdateGroup : public UpdateOnClone
{
  typedef std::vector< UpdateOnClone * > Members;

  UpdateGroup() : members() {}

  void add(UpdateOnClone *mem)
  {
    members.push_back(mem);
  }

  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap)
  {
    for(Members::iterator i=members.begin(), e=members.end(); i!=e; ++i)
      (*i)->contextRenamedViaClone(changedContext,vmap,cmap,amap);
  }

private:
  Members members;
};


}
}

#endif
