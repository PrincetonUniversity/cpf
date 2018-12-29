// De-virtualization analysis.
// Determine possible targets of indirect calls.

// This pass uses three analyses to determine the potential targets of an
// indirect call:
//
// - A flow-insensitive pass which determines which functions are
// type-compatible with the callee.  This is okay w.r.t. the published C
// semantics.  This analysis *always* succeeds, but is not always precise.  It
// sometimes requires a 'default' case.  This must be code generated as a
// sequence of compare-and-branches.  (see studyCallSite)
//
// - A flow-sensitive, partially field-/element-sensitive analysis which tries
// to track data flow to the point of the indirect call.  This is very precise,
// yet frequently fails.  This must be code generated as a sequence of
// compare-and-branches.  (see Tracer::traceConcreteFunctionPointers)
//
// - An analysis which recognizes a particular idiom: Calling the result of
// indexing into a constant initializer.  This is *ideal*, since it results in
// a mapping from integer index to function pointer, and thus can be code
// generated as a switch instruction.  The others cannot do that.  (see
// recognizeLoadFromConstantTableIdiom)

#ifndef LLVM_LIBERTY_ANALYSIS_DEVIRTUALIZE_H
#define LLVM_LIBERTY_ANALYSIS_DEVIRTUALIZE_H

#include "llvm/Pass.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/DataLayout.h"

#include "liberty/Utilities/CallSiteFactory.h"

#include <map>
#include <set>
#include <vector>

namespace liberty
{
using namespace llvm;

struct DevirtualizationAnalysis : public ModulePass
{
  static char ID;
  DevirtualizationAnalysis() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &mod);

  typedef std::vector<Function*> FcnList;

  // How are we going to devirtualize a particular
  // indirect call?
  struct Strategy
  {
    // What kind of dispatch condition
    // can we generate?
    enum DispatchType
    {
      CompareAndBranch=0,
      LoadFromConstantTableViaIndex
    };
    DispatchType dispatch;

    // Do we need to generate a default 'case'
    // which falls-back to an indirect call?
    bool        requiresDefaultCase;

    // List of possible targets.
    // If (!requiresDefaultCase): this list is exhaustive.
    // If (type==CompareAndBranch): this list is interpretted as a set.
    // If (type==LoadFromConstantTableViaIndex):
    //   this list is an ordered list, mapping
    //   the index (case value) to the target function.
    //   The list may contain null elements or duplicates.
    FcnList     callees;

    // If (type==LoadFromConstantTableViaIndex):
    //   index holds the integer value that controls the switch.
    Value      *index;
  };

  typedef std::map<Instruction*,Strategy> Inst2Strategy;
  typedef Inst2Strategy::iterator iterator;
  typedef Inst2Strategy::const_iterator const_iterator;

  unsigned size() const { return candidates.size(); }

  iterator begin() { return candidates.begin(); }
  iterator end() { return candidates.end(); }

  const_iterator begin() const { return candidates.begin(); }
  const_iterator end() const { return candidates.end(); }

  iterator find(Instruction *call) { return candidates.find(call); }
  const_iterator find(Instruction *call) const { return candidates.find(call); }

private:
  typedef std::set<const Value*> ValueSet;
  typedef std::pair<Type*,Type*> TyTy;
  typedef std::map<TyTy,bool> TypeEquivalence;

  Inst2Strategy candidates;
  TypeEquivalence equivalentTypes;

  void studyModule(Module &mod);
  void studyFunction(Function *fcn);
  void studyCallSite(CallSite &cs, Strategy &output, const DataLayout &DL);

  /// Determine whether targetting 'fcn' from 'cs'
  /// constitutes a well defined behavior w.r.t. C-spec section 6.5.2.2
  bool areTypesWeaklyCompatible(Function *fcn, CallSite &cs);

  /// Like areTypesWeaklyCompatible, except additionally
  /// ensure that the callsite does not have too many actuals.
  bool areTypesStrictlyCompatible(Function *fcn, CallSite &cs);

  // Determine if the types ty1 and ty2 are structurally equivalent,
  // and (transitively) that all pointer-fields refer to structurally
  // equivalent types.
  bool areStructurallyEquivalentTransitively(Type *ty1, Type *ty2);

  /// Determine if the supplied type is a wildcard (match anything) type
  bool isWildcard(Type *ty) const;

  // Recognize the load-from-constant-table-via-index idiom.
  // In this pattern, an integer index is used to find a function
  // pointer within a constant table.  This pattern is very common
  // in 403.gcc.  Additionally, it is preferable, since it allows
  // us to generate a SwitchInst, while other results require us
  // to use a sequence of compare-and-branches.
  bool recognizeLoadFromConstantTableIdiom(CallSite &cs, Strategy &output);
};

}
#endif
