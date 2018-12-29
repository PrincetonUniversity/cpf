// Function-capture analysis
// Identify functions whose address is captured.
//
// The compiler can enumerate all callsites for internal
// functions which are not captured.
// Exploit that fact for aggressive data flow analysis
// - traceConcreteIntegerValues
// - traceConcreteFunctionPointers

#ifndef LLVM_LIBERTY_ANALYSIS_NO_CAPTURE_FUNCTIONS_H
#define LLVM_LIBERTY_ANALYSIS_NO_CAPTURE_FUNCTIONS_H

#include "llvm/Pass.h"
#include "llvm/IR/Value.h"

#include <map>
#include <set>
#include <vector>

namespace liberty
{
using namespace llvm;

struct NoCaptureFcn : public ModulePass
{
  static char ID;
  NoCaptureFcn() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &mod);

  bool isCaptured(const Function *fcn) const;

  typedef std::vector<Function*> FcnList;
  typedef std::map<unsigned,FcnList> MinArity2FcnList;
  typedef MinArity2FcnList::const_iterator iterator;

  iterator begin(unsigned arity=~0u) const;
  iterator end(unsigned arity=~0u) const;

private:
  typedef std::set<const Value*> ValueSet;
  MinArity2FcnList captured;

  void setCaptured(Function *fcn);

  /// This is a flow-insensitive test of whether the function 'fcn'
  /// may ever be called indirectly via an address-capture test.
  /// If the address of a function is never captured, then no function
  /// pointer may contain that address.
  bool functionAddressMayBeCaptured(Function *fcn) const;

  /// Is this value only used as the target
  /// of a call (possibly via a constant cast).
  bool onlyUsedAsCallTarget(Value *v) const;
  bool onlyUsedAsCallTarget(Value *v, ValueSet &already) const;
};

}

#endif

