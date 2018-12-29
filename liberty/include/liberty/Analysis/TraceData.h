// Trace the flow of data
// to enumerate concrete values.

// This is like GetUnderlyingObjects,
// but it also takes advantage of a few
// analyses to be more aggressive.

#ifndef LLVM_LIBERTY_ANALYSIS_TRACE_DATA_H
#define LLVM_LIBERTY_ANALYSIS_TRACE_DATA_H

#include "llvm/IR/Value.h"
#include "llvm/IR/DataLayout.h"

#include <map>
#include <set>
#include <vector>

namespace liberty
{
using namespace llvm;

class NonCapturedFieldsAnalysis;
class NoCaptureFcn;

struct Tracer
{
  Tracer(const NoCaptureFcn &nocap, const NonCapturedFieldsAnalysis &noescape);

  typedef std::set<uint64_t> IntSet;
  typedef std::vector<Function*> FcnList;

  /// Trace the possible values of an integer expression.
  bool traceConcreteIntegerValues(Value *int_expr, IntSet &output) const;

  /// This is a flow-sensitive test to determine the possible
  /// concrete addresses that may flow to this pointer value.
  /// Put all possible targets into 'output' in ascending order.
  /// Return true only if we are sure that the returned list is
  /// exhaustive.  Return false if there is any chance that we
  /// missed one.
  bool traceConcreteFunctionPointers(Value *fcn_ptr, FcnList &output,
                                    const DataLayout &DL) const;

  /// Compare two sets for disjointness
  static bool disjoint(const IntSet &a, const IntSet &b);

private:
  const NoCaptureFcn &nocap;
  const NonCapturedFieldsAnalysis &noescape;

  typedef std::set<const Value*> ValueSet;
  bool traceConcreteIntegerValues(Value *int_expr, IntSet &output, ValueSet &already) const;

  bool traceConcreteFunctionPointers(
    Value *fcn_ptr, FcnList &output, ValueSet &already,
                        const DataLayout &DL) const;
  bool traceConcreteFunctionPointersLoadedFrom(
    Value *ptr, FcnList &output, ValueSet &already,
                        const DataLayout &DL) const;
  bool extractConcreteFunctionPointersFromConstantInitializer(
    Constant *constant, FcnList &output, ValueSet &already) const;
};

}
#endif
