#ifndef LLVM_LIBERTY_SPEC_PRIV_FOLD_MANAGER_H
#define LLVM_LIBERTY_SPEC_PRIV_FOLD_MANAGER_H

#include "liberty/PointsToProfiler/Pieces.h"
#include "liberty/Speculation/FoldManager.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

typedef std::map<const Ctx *, const Ctx *> CtxToCtxMap;
typedef std::map<const AU *, AU *> AuToAuMap;

struct FoldManager
{
  FoldManager() {}
  ~FoldManager();

  AU *fold(AU *);
  Ctx *fold(Ctx *);

  typedef FoldingSet< Ctx > CtxManager;
  typedef FoldingSet< AU > AUManager;
  typedef CtxManager::const_iterator ctx_iterator;

  ctx_iterator ctx_begin() const
  { return ctxManager.begin(); }

  ctx_iterator ctx_end() const
  { return ctxManager.end(); }

  /// Indicate that a context has been
  /// cloned to a new name via function duplication,
  // Update the fold manager
  // to eliminate all references (transitively) to
  // the old context, and replace them with a new
  // context via the vmap.  Populate the cmap and amap
  // as outputs.
  void cloneContext(
    // Inputs
    const Ctx *ctx,
    const ValueToValueMapTy &vmap,
    // Outputs
    CtxToCtxMap &cmap,
    AuToAuMap &amap);

  /// Indicate that a context has been cloned to
  /// a new name via inlining, and that both names
  /// may be used going forward.
  // Populate cmap and amap with the correspondence
  // between analogous contexts and AUs.
  void inlineContext(
    // Inputs
    const Ctx *ctx,
    const ValueToValueMapTy &vmap,
    // Outputs
    CtxToCtxMap &cmap,
    AuToAuMap &amap);

private:
  // Memory management and canonicalization of result objects
  AUs             allAUs;
  CtxManager      ctxManager;
  AUManager       auManager;
};

}
}

#endif

