#ifndef LLVM_LIBERTY_GET_CALLERS_H
#define LLVM_LIBERTY_GET_CALLERS_H

#include "llvm/IR/CallSite.h"

namespace liberty
{
  using namespace llvm;

  typedef std::vector<CallSite> CallSiteList;


  /// Attempt to assemble a list of all callsites
  /// which call the supplied function.  Return
  /// true if we can assume that the list is
  /// exact.  Return false if there may be some
  /// other callsite which could not be found.
  /// Use this instead of llvm's CallGraph because
  /// (1) it doesn't require the pass manager, and
  /// (2) it's aware of FULL_UNIVERSAL
  bool getCallers(const Function *fcn, CallSiteList &callsitesOut);
}

#endif

