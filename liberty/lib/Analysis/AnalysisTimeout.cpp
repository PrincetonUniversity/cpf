#include "llvm/Support/CommandLine.h"

namespace liberty
{
  using namespace llvm;

  cl::opt<unsigned> AnalysisTimeout(
    "cdc-timeout", cl::init(0), cl::Hidden,
    cl::desc("Impose a timeout (seconds) on the callsite depth combinator"));
}

