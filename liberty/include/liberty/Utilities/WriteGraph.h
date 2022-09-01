#ifndef WRITE_GRAPH_H
#define WRITE_GRAPH_H
#include "llvm/Support/DOTGraphTraits.h"
#include "noelle/core/DGGraphTraits.hpp"
#include "scaf/Utilities/ReportDump.h"

using namespace llvm;
using namespace llvm::noelle;
template <class GT, class T> void writeGraph(std::string filename, GT *graph) {
  std::error_code EC;
  // limit the string to 200 characters (256 bytes limit for Linux)
  if (filename.length() > 200) {
    filename = filename.substr(0, 200);
  }
  filename += ".dot";

  raw_fd_ostream File(filename, EC, sys::fs::F_Text);
  std::string Title = DOTGraphTraits<GT *>::getGraphName(graph);

  DGGraphWrapper<GT, T> graphWrapper(graph);


  if (!EC) {
    WriteGraph(File, &graphWrapper, false, Title);
  } else {
    REPORT_DUMP(errs() << "Error opening file for writing!\n");
    abort();
  }
}

template <class GT> void writeGraph(std::string filename, GT *graph) {
  writeGraph<GT, Value>(filename, graph);
}
#endif
