// LoopAA is a supplement to llvm::AliasAnalysis.
//
//
// So, surely you're asking: why are you yicking with
// llvm::AliasAnalysis?
//
// (1) Sometimes you want to answer the question:
//     is there a dependence between two operations
//     ever, even in different iterations of a loop
//     which contains them?
//
//     llvm::AliasAnalysis does not support this query,
//     since several implementations (-basicaa, -scev-aa)
//     answer the question only within the same iteration.
//
//     For instance:
//         for(i=1; i<n; ++i)
//           array[i] = array[i-1];
//
//     According to -basicaa, there is no alias, since
//     within one iteration, the load and store are disjoint.
//     However, there is clearly a loop-carried dependence;
//     iteration i=1 defines array[1], and iteration i=2
//     reads array[1].
//
//     The most accurate way to describe the semantics
//     of llvm::AliasAnalysis is that for each query,
//     it chooses the specific iteration of every loop in
//     the loop nest which gives the most conservative
//     result.  Different queries may draw their answer
//     from different iterations.  We consider this unacceptable.
//
// (2) Sometimes you want to answer the question:
//     is there a dependence between two operations
//     during two different iterations of a loop which
//     contains them?
//
//     llvm::AliasAnalysis does not support this query;
//     the interface does not allow you to specify a
//     loop.
//
// (3) One may ask: aren't the results of [query 1]
//     simply the union of the results of
//     llvm::AliasAnalysis and the results of
//     [query 2]?
//
//     Answer: only if the results of [query 2] are precise.  But,
//
//       (i) alias analyses are necessarily approximations---not precise.
//
//       (ii) loop-carried dependence analysis tends to be more expensive
//         as it must be (in theory) at least partially path sensitive,
//         and so we'd prefer to first filter with [query 1] to reduce
//         the number of calls to [query 2].  We cannot use llvm::AliasAnalysis
//         to filter these queries, since they may or may-not be
//         loop-sensitive.
//
//     Answer 2: it would make recurrent queries harder.
//
//         Some alias analysis questions, loop-sensitive or not,
//         can be phrased as "a may-alias b iff f(a) may-alias f(b),"
//         where f is some simple pointer transform.  As an informal
//         example,  two array accesses a[k], b[k] alias iff a and b alias.
//         However, we need to have consistent semantics in order to
//         perform such recurrent queries.
//
// Having said all of that, there are still some design concerns worth
// mentioning:
//
// (1) These queries can only be performed if you have an llvm::Loop object.
//     These objects are introduced by the llvm::LoopInfo pass, which is
//     an llvm::FunctionPass.  This can make scheduling of LoopAAs difficult.
//     In particular, if a module pass transform does a getAnalysis<LoopInfo>(),
//     llvm will schedule a new LoopInfo pass, private to the module pass,
//     and possibly different than the one which (for instance) feeds into
//     the LoopAA implementation.  LoopAA implementations are discouraged
//     from holding reference to llvm::LoopInfo or llvm::Loop objects.
//
//     On the plus side, the interface technically only requires such
//     objects to be live when LoopAA is queried; several LoopAAs, including
//     AcyclicAA, are able to perform their analysis before loops are
//     computed.
//

// Warning:
// To use this, you need to use this idiom:
//
//    LoopAA *loopaa = getAnalysis< LoopAA >().getTopAA();
//
// The reason is that llvm's getAnalysis<> will give you a reference to the
// last LoopAA to run, but not necessarily to the top of the stack!

#ifndef LLVM_LIBERTY_LOOP_AA_H
#define LLVM_LIBERTY_LOOP_AA_H

#include "llvm/Pass.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"

#include "Assumptions.h"

namespace liberty
{
  using namespace llvm;

  extern cl::opt<bool> FULL_UNIVERSAL;

  /// This class defines an interface for chained alias analyses
  /// with well-defined semantics around loops.
  ///
  class LoopAA
  {
  public:
    /// UnknownSize - This is a special value which can be used with the
    /// size arguments in alias queries to indicate that the caller does not
    /// know the sizes of the potential memory references.
    static unsigned const UnknownSize = ~0u;

    static char ID;
    LoopAA();
    virtual ~LoopAA();

    /// This is the result of an alias query.
    enum AliasResult
    {
      NoAlias   = 0,
      MayAlias  = 3,    // MayAlias & MustAlias == MustAlias
      MustAlias = 1
    };
    /// This is the result of a mod-ref query.
    enum ModRefResult
    {
      NoModRef  = 0,
      Ref       = 1,
      Mod       = 2,
      ModRef    = 3
    };

    /// This is the desired result of an alias query.
    enum DesiredAliasResult
    {
      DNoAlias   = 1,
      DMustAlias = 2,
      DNoOrMustAlias  = 3
    };

    //typedef std::pair<AliasResult, Remedies> AliasResultFull;
    //typedef std::pair<ModRefResult, Remedies> ModRefResultFull;

    /// The temporal relationship between two pointer
    /// accesses or two operations.  Time is measured
    /// in terms of iterations of the provided loop.
    enum TemporalRelation {
      Before = 0, // Strictly before; I(A) < I(B) and I(A) != I(B)
      Same = 1,   // Equal; I(A) == I(B)
      After = 2   // Strictly after; I(A) > I(B) and I(A) != I(B).
    };

    /// How should we schedule this LoopAA?
    /// Some LoopAAs have a high per-query cost,
    /// others have a low per-query cost.
    /// We want the low cost implementations
    /// higher in the stack, so they can
    /// solve the easy ones.
    /// Choose any SchedulingPreference
    /// between Bottom and Top.
    enum SchedulingPreference
    {
      Bottom    = 1,
      Low       = 25,
      Normal    = 50,
      Top       = 100
    };


    static TemporalRelation Rev(TemporalRelation);

    /// Find the scheduling preference for this
    /// implementation.  Most should leave this unchanged.
    virtual SchedulingPreference getSchedulingPreference() const;


    /// Three methods:
    ///
    ///   pointer-vs-pointer   (alias)
    ///   operator-vs-pointer  (modref)
    ///   operator-vs-operator (modref)
    ///
    /// Where pointer is a (value,size) pair,
    /// and operator is an instruction which affects memory.

    /// This is the method that consumers of
    /// dependence analysis will call.  It means:
    ///
    /// Does the pointer ptrA of size sizeA, accessed in iteration I(A),
    /// alias with the pointer ptrB of size sizeB, accessed in iteration I(B).
    ///
    /// The result is a one of:
    ///
    ///   NoAlias   :    These pointers do not alias when I(A) rel I(B).
    ///   MayAlias  :    These pointers may alias when I(A) rel I(B).
    ///   MustAlias :    These pointers must alias when I(A) rel I(B).
    ///
    /// Also, when implementations of LoopAA chain to
    /// lower LoopAAs, they call this method in the
    /// base class.
    ///
    /// Do not be surpised:
    ///   This is NOT a transitive relation.
    ///
    virtual AliasResult alias(const Value *ptrA, unsigned sizeA,
                              TemporalRelation rel, const Value *ptrB,
                              unsigned sizeB, const Loop *L, Remedies &remeds,
                              DesiredAliasResult dAliasRes = DNoOrMustAlias);

    /// This is the method that consumers of
    /// dependence analysis will call.  It means:
    ///
    /// Does the operation A, executing in iteration I(A),
    /// affect the memory specified by ptrB of size sizeB
    /// when accessed in iteration I(B)?
    ///
    /// The result is a one of:
    ///
    ///   NoModRef :      The operation does not access B when I(A) rel I(B).
    ///   Ref      :      The operation may read B when I(A) rel I(B).
    ///   Mod      :      The operation may write B when I(A) rel I(B).
    ///   ModRef   :      The operation may read and write B when I(A) rel I(B).
    ///
    /// Also, when implementations of LoopAA chain to
    /// lower LoopAAs, they call this method in the
    /// base class.
    ///
    /// Do not be surpised:
    ///   This is NOT an symmmetric relation.
    ///   This is NOT a reflexive relation.
    ///   This is NOT a transitive relation.
    ///
    virtual ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Value *ptrB, unsigned sizeB,
      const Loop *L,
      Remedies &remeds);


    /// This is the method that consumers of
    /// dependence analysis will call.  It means:
    ///
    /// Does the operator A, executing in iteration I(A),
    /// affect the memory accessed by operator B in iteration I(B).
    ///
    /// The result is a one of:
    ///
    ///   NoModRef :      A neither reads nor writes B's memory when I(A) rel I(B).
    ///   Ref      :      A may read B's memory when I(A) rel I(B).
    ///   Mod      :      A may write B's memory when I(A) rel I(B).
    ///   ModRef   :      A may read or write B's memory when I(A) rel I(B).
    ///
    /// Also, when implementations of LoopAA chain to
    /// lower LoopAAs, they call this method in the
    /// base class.
    ///
    /// Do not be surpised:
    ///   This is NOT an symmmetric relation.
    ///   This is NOT a reflexive relation.
    ///   This is NOT a transitive relation.
    ///
    virtual ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Instruction *B,
      const Loop *L,
      Remedies &remeds);

    virtual bool pointsToConstantMemory(const Value *P, const Loop *L);

    /// canBasicBlockModify - Return true if it is possible for execution of the
    /// specified basic block to modify the value pointed to by Ptr.
    bool canBasicBlockModify(const BasicBlock &BB,
                             TemporalRelation Rel,
                             const Value *Ptr,
                             unsigned Size,
                             const Loop *L,
                             Remedies &R);

    /// canInstructionRangeModify - Return true if it is possible for the
    /// execution of the specified instructions to modify the value pointed to
    /// by Ptr. The instructions to consider are all of the instructions in the
    /// range of [I1,I2] INCLUSIVE.  I1 and I2 must be in the same basic block.
    bool canInstructionRangeModify(const Instruction &I1,
                                   TemporalRelation Rel,
                                   const Instruction &I2,
                                   const Value *Ptr,
                                   unsigned Size,
                                   const Loop *L,
                                   Remedies &R);

    bool mayModInterIteration(const llvm::Instruction *A,
                              const llvm::Instruction *B,
                              const llvm::Loop *L,
                              Remedies &R);

    /// Get the name of this AA
    virtual StringRef getLoopAAName() const = 0;

    /// isNoAlias - A trivial helper function to check to see if the specified
    /// pointers are no-alias.
    bool isNoAlias(const Value *V1, unsigned V1Size,
                   TemporalRelation Rel,
                   const Value *V2, unsigned V2Size,
                   const Loop *L, Remedies &R) {
      Remedies tmpR;
      AliasResult res = alias(V1, V1Size, Rel, V2, V2Size, L, tmpR);
      if (res == NoAlias) {
        for (auto remed : tmpR)
          R.insert(remed);
        return true;
      }
      return false;
    }

    /// Subclasses must call this method at the beginning
    /// of runOnWhatever() to initalize the the LoopAA interface.
    void InitializeLoopAA(Pass *P, const DataLayout&);

    /// Alternatively, send this.
    /// Subclasses must call this method at the beginning
    /// of runOnWhatever() to initalize the the LoopAA interface.
    void InitializeLoopAA(const DataLayout *t, TargetLibraryInfo *tli, LoopAA *next);

    /// Subclasses should call the base implementation of
    /// their method during their override of getAnalysisUsage().
    virtual void getAnalysisUsage(AnalysisUsage &au) const;

    /// For recurrent queries, we want to ask from the
    /// top of the LoopAA stack.
    virtual LoopAA *getTopAA() const;
    LoopAA *getRealTopAA() const;

    /// Get a handle to the DataLayout object.
    const DataLayout *getDataLayout() const;

    /// Get a handle to the TargetLibraryInfo object.
    const TargetLibraryInfo *getTargetLibraryInfo() const;

    /// Print the LoopAA stack
    void print(raw_ostream &out) const;
    void dump() const;

    /// Do these two values have different function parents?
    static bool isInterprocedural(const Value *O1, const Value *O2);

    /// Tell the LoopAA stack that the stack has changed
    /// by adding/subtracting other LoopAAs.
    void stackHasChanged();

    // utilities for processing remedies
    bool containsExpensiveRemeds(const Remedies &R);
    unsigned long totalRemedCost(const Remedies &R);

    // is remeds1 cheaper than remeds2?
    bool isCheaper(Remedies &remeds1, Remedies &remeds2);

    // merge newRemeds into remeds
    void appendRemedies(Remedies &remeds, Remedies &newRemeds);

    // join results with remedies (keep cheapest or most precise option)
    ModRefResult join(Remedies &finalRemeds, ModRefResult res1,
                      Remedies &remeds1, ModRefResult res2, Remedies &remeds2);
    AliasResult join(Remedies &finalRemeds, AliasResult res1, Remedies &remeds1,
                     AliasResult res2, Remedies &remeds2);

    // Given the currently available response (result and remedies), do
    // further exploration if needed (either due to use of expensive remedies or
    // due to imprecise current answer).
    // Adjust these function to change exploration policy.
    ModRefResult chain(Remedies &finalRemeds, const Instruction *A,
                       TemporalRelation rel, const Instruction *B,
                       const Loop *L, ModRefResult curRes, Remedies &curRemeds);
    ModRefResult chain(Remedies &finalRemeds, const Instruction *A,
                       TemporalRelation rel, const Value *ptrB, unsigned sizeB,
                       const Loop *L, ModRefResult curRes, Remedies &curRemeds);
    AliasResult chain(Remedies &finalRemeds, const Value *V1, unsigned Size1,
                      TemporalRelation Rel, const Value *V2, unsigned Size2,
                      const Loop *L, AliasResult curRes, Remedies &curRemeds,
                      DesiredAliasResult dAliasRes = DNoOrMustAlias);

  protected:
    /// Called indirectly by stackHasChanged().
    virtual void uponStackChange();

    LoopAA *getNextAA() const { return nextAA; }
    unsigned getDepth();

  private:
    const DataLayout *td;
    const TargetLibraryInfo *tli;
    LoopAA *nextAA, *prevAA;
  };


  /// IO easiness
  template <class OStream>
  OStream &operator<<(OStream &out, LoopAA::TemporalRelation rel)
  {
    switch(rel)
    {
      case LoopAA::Before:
        out << "Before";
        break;
      case LoopAA::Same:
        out << "Same";
        break;
      case LoopAA::After:
        out << "After";
        break;
    }
    return out;
  }
  template <class OStream>
  OStream &operator<<(OStream &out, LoopAA::AliasResult ar)
  {
    switch(ar)
    {
      case LoopAA::NoAlias:
        out << "NoAlias";
        break;
      case LoopAA::MayAlias:
        out << "MayAlias";
        break;
      case LoopAA::MustAlias:
        out << "MustAlias";
        break;
    }
    return out;
  }
  template <class OStream>
  OStream &operator<<(OStream &out, LoopAA::ModRefResult mr)
  {
    switch(mr)
    {
      case LoopAA::NoModRef:
        out << "NoModRef";
        break;
      case LoopAA::Ref:
        out << "Ref";
        break;
      case LoopAA::Mod:
        out << "Mod";
        break;
      case LoopAA::ModRef:
        out << "ModRef";
        break;
    }
    return out;
  }



  /// This is a simple class which evaluates the effectiveness
  /// of loop aa, analogous to llvm's -aa-eval.
  class EvalLoopAA : public FunctionPass
  {
  public:
    static char ID;
    EvalLoopAA();
    ~EvalLoopAA();

    void getAnalysisUsage(AnalysisUsage &au) const;

    bool runOnFunction(Function &fcn);
  private:
    bool runOnLoop(Loop *L);

    unsigned totals[2][4],
             fcnTotals[2][4],
             loopTotals[2][4];
  };

  /// This is a very basic implementation of LoopAA
  /// which always reports may-mod-ref.  It does not chain
  /// to lower levels.  It is the default implementation
  /// of the LoopAA analysis group.
  class NoLoopAA : public LoopAA, public ModulePass
  {
  public:
    static char ID;
    NoLoopAA();

    virtual void getAnalysisUsage(AnalysisUsage &au) const;

    virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
      if (PI == &LoopAA::ID)
        return (LoopAA*)this;
      return this;
    }

    virtual bool runOnModule(Module &mod);

    virtual SchedulingPreference getSchedulingPreference() const;

    virtual StringRef getLoopAAName() const { return "NoLoopAA"; }

    virtual AliasResult alias(const Value *ptrA, unsigned sizeA,
                              TemporalRelation rel, const Value *ptrB,
                              unsigned sizeB, const Loop *L, Remedies &remeds,
                              DesiredAliasResult dAliasRes = DNoOrMustAlias);

    virtual ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Value *ptrB, unsigned sizeB,
      const Loop *L,
      Remedies &remeds);

    virtual ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Instruction *B,
      const Loop *L,
      Remedies &remeds);

    virtual bool pointsToConstantMemory(const Value *P, const Loop *L);
  };

  /// This is a wrapper around llvm::AliasAnalysis
  class AAToLoopAA : public FunctionPass, public LoopAA
  {
    AliasAnalysis *AA;

  public:
    static char ID;
    AAToLoopAA();

    virtual void getAnalysisUsage(AnalysisUsage &au) const;

    virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
      if (PI == &LoopAA::ID)
        return (LoopAA*)this;
      return this;
    }

    virtual bool runOnFunction(Function &fcn);

    virtual StringRef getLoopAAName() const { return "AAToLoopAA"; }

    virtual AliasResult alias(const Value *ptrA, unsigned sizeA,
                              TemporalRelation rel, const Value *ptrB,
                              unsigned sizeB, const Loop *L, Remedies &remeds,
                              DesiredAliasResult dAliasRes = DNoOrMustAlias);

    virtual ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Value *ptrB, unsigned sizeB,
      const Loop *L,
      Remedies &remeds);

    virtual ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Instruction *B,
      const Loop *L,
      Remedies &remeds);

    /// Conservatively raise an llvm::AliasAnalysis::AliasResult
    /// to a liberty::LoopAA::AliasResult.
    static AliasResult Raise(llvm::AliasResult);

    /// Conservatively raise an llvm::AliasAnalysis::ModRefResult
    /// to a liberty::LoopAA::ModRefResult.
    static ModRefResult Raise(llvm::ModRefInfo);


  };

}

#endif
