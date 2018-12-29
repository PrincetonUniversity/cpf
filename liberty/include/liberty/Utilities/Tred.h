#ifndef LLVM_LIBERTY_TRED_H
#define LLVM_LIBERTY_TRED_H

namespace liberty
{
  // Run the graphviz 'tred' utility.  Performs
  // transitive reduction on a graph.
  void runTred(const char *infile, const char *outfile, unsigned timeout=30u);
}

#endif

