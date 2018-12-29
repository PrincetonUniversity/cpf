#ifndef LLVM_LIBERTY_ANALYSIS_TIMEOUT_H
#define LLVM_LIBERTY_ANALYSIS_TIMEOUT_H

#include "llvm/Support/CommandLine.h"

namespace liberty
{
  using namespace llvm;
  extern llvm::cl::opt<unsigned> AnalysisTimeout;
}

#endif


