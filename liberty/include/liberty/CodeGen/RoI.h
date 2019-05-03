// Determines which functions execute within the
// parallel region, and which do not.  Responsible
// for ensuring that these two classes of functions
// are disjoint via function specialization
#ifndef LLVM_LIBERTY_SPEC_PRIV_REGION_OF_INTEREST_H
#define LLVM_LIBERTY_SPEC_PRIV_REGION_OF_INTEREST_H

#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"

#include <set>
#include <map>

#include "liberty/Speculation/FoldManager.h"
#include "liberty/Speculation/Selector.h"
#include "liberty/Speculation/UpdateOnClone.h"
#include "liberty/Utilities/MakePtr.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

/// Region of interest
struct RoI
{
  typedef std::set<BasicBlock*> BBSet;
  typedef std::set<Function*> FSet;

  // An exact set of basic blocks which constitute the Region of Interest (RoI)
  BBSet bbs;

  // A set of functions which intersect the RoI.
  // Stated another way, these are the functions which
  // were modified to add misspeculation tests, and
  // which /should/ be backed up to support functions
  // that are shared by the parallel and non-parallel regions.
  // Invariant: Forall b in roiBB, b->parent in roiF.
  // But not: Forall f in roiF, b in f->bbs, b in roiBB.
  FSet fcns;

  // A set of basic blocks within loops which are "roots" of the region of interest
  // (seed of the initial sweep call)
  BBSet roots;

  void clear();

  template <class BlockIterator>
  void sweep(const BlockIterator begin, const BlockIterator end)
  {
    for(BlockIterator i=begin; i!=end; ++i)
      sweep( MakePointer(*i) );
  }

  void sweep(BasicBlock *);

  template <class BlockIterator>
  void addRoots(const BlockIterator begin, const BlockIterator end)
  {
    for(BlockIterator i=begin; i!=end; ++i)
      roots.insert(*i);
  }

  bool resolveSideEntrances(UpdateOnClone &, FoldManager &, Selector &);

  void print(raw_ostream &) const;
  void dump() const;

private:
  typedef std::map<const Function*, Function*> F2F;
  typedef std::map<const Function*, ValueToValueMapTy*> F2VM;

  std::map<const Function*, bool> reachability_cache;

  bool isReachableFromOutsideOfRoI(Function*);

  bool isCloneOrCloned(Function *, F2F &);

  Function* getOriginal(Function *, F2F &);

  bool cloneRootsIfNecessary(UpdateOnClone &, FoldManager &, F2F &, F2VM &, FSet &);

  bool resolveOneSideEntrance(UpdateOnClone &, FoldManager &, F2F &, F2VM &);

  void swapRootFcnUses(UpdateOnClone &, FoldManager &, F2F &, F2VM &, FSet &);

  Function* genMappingFunction(Module *, std::string, Type *, F2F &);

  void replaceIndirectCall(std::map<Type*, Function*> &, CallInst *, Selector &);

  void createO2CFunctions(F2F &, Selector &);

  void addToLPS(Selector &, Instruction *, Instruction *);
};

}
}

#endif

