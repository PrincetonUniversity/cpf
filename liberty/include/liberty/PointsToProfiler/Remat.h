// Responsible for rematerialization of values.
#ifndef LLVM_LIBERTY_SPEC_PRIV_REMAT_H
#define LLVM_LIBERTY_SPEC_PRIV_REMAT_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/PointsToProfiler/Pieces.h"
#include "liberty/Utilities/InstInsertPt.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{

class Read;
class HeapAssignment;

struct Remat
{
  bool canRematInHeader(const Value *ptr, const Loop *loop) const;

  /* Deprecated -- only used by SpecPriv::Transform, which itself is deprecated
  bool canRematInHeader(const Value *ptr, const Loop *loop, const Read &spresults, const HeapAssignment &asgn) const;
   */

  bool canRematAtEntry(const Value *ptr, const Function *fcn) const
  {
    std::set<const Value*> already;
    return canRematAtEntry(ptr,fcn,already);
  }

  bool canRematAtBlock(const Value *ptr, const BasicBlock *bb, const DominatorTree *dt = 0) const
  {
    std::set<const Value*> already;
    return canRematAtBlock(ptr,bb,dt,already);
  }

  Value *rematAtBlock(InstInsertPt &where, Value *ptr, const DominatorTree *dt = 0)
  {
    assert( canRematAtBlock(ptr, where.getBlock(), dt) && "That value cannot be rematerialized there");

    return rematUnsafe(where,ptr,0,dt);
  }

  Value *rematInHeader(InstInsertPt &where, Value *ptr, const Loop *loop)
  {
    assert( canRematInHeader(ptr,loop) );
    return rematUnsafe(where,ptr,loop);
  }

  Value *rematAtEntry(InstInsertPt &where, Value *ptr, const Function *fcn)
  {
    assert( canRematAtEntry(ptr,fcn) );
    return rematUnsafe(where,ptr,0);
  }

  // Perform rematerialization of the value 'ptr' at point 'where'
  // within loop.  Does not first assert that this is possible.
  Value *rematUnsafe(InstInsertPt &where, Value *ptr, const Loop *loop, const DominatorTree *dt=0);

  uint64_t evaluateToInteger(const SCEV *s, const DataLayout &td) const
  {
    assert( canEvaluateToInteger(s) );
    return evaluateToIntegerUnsafe(s,td);
  }

  bool canEvaluateToInteger(const SCEV *s) const;

  uint64_t evaluateToIntegerUnsafe(const SCEV *s, const DataLayout &td) const;

  Value *remat(InstInsertPt &where, ScalarEvolution &SE, const SCEV *s, const DataLayout &td) const;
  Value *remat(InstInsertPt &where, ScalarEvolution &SE, const SCEV *s, const DataLayout &td, Type *ty) const;

private:
  typedef DenseMap<const Value *, Value *> Clones;
  typedef std::map<const Loop *, Clones> Loop2Clones;
  Loop2Clones loop2clones;

  bool canRematAtEntry(const Value *ptr, const Function *fcn, std::set<const Value*> &already) const;
  bool canRematInHeader(const Value *ptr, const Loop *loop, std::set<const Value*> &already) const;
  bool canRematAtBlock(const Value *ptr, const BasicBlock *bb, const DominatorTree *dt, std::set<const Value*> &already) const;
};


}
}

#endif

