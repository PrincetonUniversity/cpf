#ifndef LLVM_LIBERTY_SPEC_PRIV_DISCRIMINATOR_H
#define LLVM_LIBERTY_SPEC_PRIV_DISCRIMINATOR_H

#include "liberty/SpecPriv/Pieces.h"

#include "Classify.h"

namespace liberty
{
namespace SpecPriv
{

// This class is basically an inverted view of a HeapAssignment object.
// Instead of mapping heaps to lists of AUs (specified by a static+dynamic name),
// This maps static AU names to heaps, given a calling context.
// The purpose is to determine /how much/ calling context is necessary
// to disambiguate the appropriate heap assignment.
struct Discriminator
{
  typedef std::pair<HeapAssignment::Type, Reduction::Type> HeapSpec;
  typedef std::multimap<HeapSpec, const Ctx *> HeapGivenContext;

//  Discriminator() : static2heap() {}
  Discriminator(const HeapAssignment &a);

  const HeapGivenContext &classify(const Value *ptr) const;

  bool resolveAmbiguitiesViaCloning(UpdateOnClone &changes, FoldManager &fmgr);

private:

  const HeapAssignment &asgn;

  typedef std::map< const Value *, HeapGivenContext >  Value2HeapGivenContext;
  Value2HeapGivenContext static2heap;

  void recompute();

  typedef Value2HeapGivenContext::const_iterator iterator;
  typedef HeapGivenContext::const_iterator group_iterator;

  void addAUs(HeapAssignment::Type heap, const HeapAssignment::AUSet &aus);
  void addAUs(HeapAssignment::Type heap, const HeapAssignment::ReduxAUSet &raus);

  unsigned determineShortestSuffix(const HeapGivenContext &heapGivenCtx) const;

  bool resolveOneAmbiguityViaCloning(UpdateOnClone &changes, FoldManager &fmgr);
  void resolveGroupAmbiguity(unsigned, const group_iterator &, const group_iterator &, UpdateOnClone &changes, FoldManager &fmgr);

};

}
}

#endif

