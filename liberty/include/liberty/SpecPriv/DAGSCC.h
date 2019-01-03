#ifndef LLVM_LIBERTY_SPEC_PRIV_DAG_SCC_H
#define LLVM_LIBERTY_SPEC_PRIV_DAG_SCC_H

#include "llvm/ADT/BitVector.h"

#include "liberty/Utilities/BitMatrix.h"
#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/PDG.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

struct SCCs
{
  typedef std::vector<Vertices::ID>   SCC;
  typedef std::vector<SCC>            SCCList;
  typedef SCCList::const_iterator     iterator;
  typedef std::vector<unsigned>       SCCSet;

  SCCs(const PDG &pdg);
  void recompute(const PDG &pdg);

  // Enumerate strongly connected components in
  // reverse topological order.
  iterator begin() const { return sccs.begin(); }
  iterator end() const { return sccs.end(); }

  void setSequential(const SCC &scc);
  void setSequential(Vertices::ID v);
  void setParallel(const SCC &scc);
  void setParallel(Vertices::ID v);
  bool mustBeInSequentialStage(const SCC &scc) const;
  bool mustBeInSequentialStage(Vertices::ID v) const;
  void print_scc_dot(raw_ostream &fout, unsigned sccnum) const;
  void print_replicated_scc_dot(raw_ostream &fout, unsigned sccnum, unsigned stage, bool first) const;
  void print_dot(raw_ostream &fout) const;
  void print_dot(const PDG &pdg, StringRef dotFileName, StringRef tredFileName, bool bailout=false) const;
  //void print_dot(const PDG &pdg, const char *dotFileName, const char *tredFileName, bool bailout=false) const;
  void getUpperBoundParallelStage(const PDG &pdg, std::vector<Instruction*> &insts) const;

  bool hasNonTrivialParallelStage(const PDG &pdg) const;

  // Does 'scc' have an edge to any of 'sccs'
  static bool hasEdge(const PDG &pdg, const SCC &scc, const SCCList &sccs);
  // Do any of 'sccs' have an edge to 'scc'
  static bool hasEdge(const PDG &pdg, const SCCList &sccs, const SCC &scc);
  // Does 'a' have an edge to 'b'
  static bool hasEdge(const PDG &pdg, const SCC &a, const SCC &b);

  unsigned size() const { return sccs.size(); }
  const SCC &get(unsigned i) const;

  void computeReachabilityAmongSCCs(const PDG &pdg);

  bool orderedBefore(unsigned earlySCC, const SCCSet &lates) const;
  bool orderedBefore(const SCCSet &earlies, unsigned lateSCC) const;
  bool orderedBefore(unsigned earlySCC, unsigned lateSCC) const;

  static bool computeDagScc(PDG &pdg, SCCs &sccs, bool abortIfNoParallelStage = true);

  static void markSequentialSCCs(PDG &pdg, SCCs &sccs);

  // This is an evil constructor.  Try not to use it.  It exists only to
  // facilitate experiments, e.g. to convert a Exp_PDG_NoTiming, etc, into a
  // real PDG object.
  SCCs(SCCList &sccs,  BitVector &inSequentialStage);

private:
  typedef std::vector<int>          Vertex2Index;
  typedef std::vector<Vertices::ID> Stack;

  // These are only used during construction
  unsigned        index;
  Vertex2Index    idx, low;
  Stack           stack;
  //

  SCCList         sccs;
  BitVector       inSequentialStage;

  bool            ordered_dirty;
  BitMatrix       ordered;

  void visit(Vertices::ID vertex, const PartialEdgeSet &G);
  void stable_sort_sccs(const PDG &pdg);
};

raw_ostream &operator<<(raw_ostream &fout, const SCCs &sccs);

}
}

#endif

