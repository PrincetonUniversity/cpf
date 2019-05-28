// This class is responsible for interpretting
// profile results and to determine a heap assignment
// for every hot loop in the benchmark.
#ifndef LIBERTY_SPEC_PRIV_CLASSIFY_H
#define LIBERTY_SPEC_PRIV_CLASSIFY_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"

#include "liberty/Analysis/CallsiteSearch.h"
#include "liberty/Speculation/Read.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

/// Represents a partitioning of the loop's memory footprint by access pattern
struct HeapAssignment : public UpdateOnClone
{
  enum Type {
      ReadOnly=0,
      Shared,
      Redux,
      Local,
      Private,
      Unclassified,
    FirstHeap=ReadOnly, LastHeap=Private, NumClassifications=Unclassified+1 };

  struct ReduxDepInfo {
    AU *depAU;
    Reduction::Type depType;
  };

  typedef std::set<AU*> AUSet;
  typedef std::map<AU*,Reduction::Type> ReduxAUSet;
  typedef std::map<AU*,ReduxDepInfo> ReduxDepAUSet;
  typedef std::map<const AU*,int> SubheapAssignment;
  typedef std::set<const BasicBlock *> LoopSet;
  typedef LoopSet::const_iterator loop_iterator;

  loop_iterator loop_begin() const { return success.begin(); }
  loop_iterator loop_end() const { return success.end(); }

  // Determine if the loop was successfully classified
  bool isValid() const;
  bool isValidFor(const Loop *) const;

  // Determine if each the AUs from each category
  // {shared,local,private,redux,ro} can be statically
  // distinguished; i.e. context is unnecessary.
  bool isSimpleCase() const;

  /// Classify an AU
  Type classify(AU *) const;

  // Find a consistent classification for a set of AUs
  Type classify(Ptrs &) const;

  /// Classify a pointer w.r.t. this loop
  Type classify(const Value *, const Loop *, const Read &) const;

  const AUSet &getSharedAUs() const;
  const AUSet &getLocalAUs() const;
  const AUSet &getPrivateAUs() const;
  const AUSet &getReadOnlyAUs() const;
  const ReduxAUSet &getReductionAUs() const;
  const ReduxDepAUSet &getReduxDepAUs() const;

  /// Return the subheap for an object, or (-1) on failure.
  int getSubHeap(const AU *au) const;
  void setSubHeap(const AU *au, int sh);

  /// Find a consistent subheap for a set of AUs
  int getSubHeap(Ptrs &) const;

  /// Find a subheap for this pointer w.r.t. this loop
  int getSubHeap(const Value *, const Loop *, const Read &) const;

  AUSet &getSharedAUs();
  AUSet &getLocalAUs();
  AUSet &getPrivateAUs();
  AUSet &getReadOnlyAUs();
  ReduxAUSet &getReductionAUs();
  ReduxDepAUSet &getReduxDepAUs();

  void setValidFor(const Loop *);

  void print(raw_ostream &) const;
  void dump() const;

  static Type join(Type a, Type b);

  // A reflexive, transitive, but ASYMMETRIC relation.
  // See also: Selector::compatible()
  bool compatibleWith(const HeapAssignment &other) const;

  // This operator creates a new heap assignment from
  // two compatible heap assignments.  Assuming compatibility,
  // this operator is associative and commutative.
  HeapAssignment operator&(const HeapAssignment &other) const;

  // Update on clone
  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &au);

  void assignSubHeaps();

private:

  /// Set of loops on which this assignment is valid
  LoopSet success;

  /// Sets of shared,local,private and read-only AUs
  /// indexed by loop within this function.
  AUSet shareds, locals, privs, ros;
  ReduxAUSet reduxs;
  ReduxDepAUSet reduxdeps;

  /// Sub-heap assignments.
  SubheapAssignment subheaps;

  bool compatibleWith(Type ty, const AUSet &set) const;
  bool compatibleWith(Type ty, const ReduxAUSet &rset) const;

  void accumulate(const HeapAssignment &A, Type ty, const AUSet &aus);
  void accumulate(const HeapAssignment &A, Type ty, const ReduxAUSet &raus);

  void updateAUSet(AUSet &aus, const AuToAuMap &amap);
  void updateAUSet(ReduxAUSet &aus, const AuToAuMap &amap);

  template <class Collection>
  void assignSubHeaps(Collection &c);
};

// Reflexive, Symmetric and Transitive.
bool compatible(const HeapAssignment &A, const HeapAssignment &B);

raw_ostream &operator<<(raw_ostream &, const HeapAssignment &);

/// Determine a heap assignment for each hot loop
struct Classify : public ModulePass, public UpdateOnClone
{
  static char ID;
  Classify() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

  bool isAssigned(const Loop *) const;
  const HeapAssignment &getAssignmentFor(const Loop *) const;

  typedef std::map<const BasicBlock *, HeapAssignment> Loop2Assignments;
  typedef Loop2Assignments::const_iterator iterator;

  iterator begin() const { return assignments.begin(); }
  iterator end() const { return assignments.end(); }

  // Update on clone
  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &au);

private:
  Loop2Assignments assignments;

  bool runOnLoop(Loop *loop);

  // Look-up all AUs which carry dependences ACROSS loop.
  // Return false only if the set of AUs cannot be determined
  // (it may successfully return an empty set).
  bool getLoopCarriedAUs(Loop *loop, const Ctx *ctx, AUs &aus) const;

  // Look-up the AUs which carry flow dependences from src to dst ACROSS loop.
  // src,dst may be any operation, including callsites...
  bool getUnderlyingAUs(Loop *loop,
    ReverseStoreSearch &search_src, Instruction *src, const Ctx *src_ctx,
    Instruction *dst, const Ctx *dst_ctx, AUs &aus) const;

  // Look-up the AUs which carry flow dependences from src to dst ACROSS loop.
  bool getUnderlyingAUs(
    const CtxInst &src, const Ctx *src_ctx,
    const CtxInst &dst, const Ctx *dst_ctx,
    AUs &aus) const;
};

}
}

#endif

