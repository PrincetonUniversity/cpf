#ifndef LLVM_LIBERTY_SPEC_PRIV_DAG_SCC_EXP_NO_TIMING_H
#define LLVM_LIBERTY_SPEC_PRIV_DAG_SCC_EXP_NO_TIMING_H

#include "llvm/ADT/BitVector.h"

#include "liberty/Utilities/BitMatrix.h"
#include "liberty/Speculation/ControlSpeculator.h"

#include "Exp.h"
#include "Exp_PDG_NoTiming.h"

namespace liberty
{
namespace SpecPriv
{
struct SCCs;

namespace FastDagSccExperiment
{
using namespace llvm;

struct Exp_SCCs_NoTiming
{
  typedef std::vector<Vertices::ID>   SCC;
  typedef std::vector<SCC>            SCCList;
  typedef SCCList::const_iterator     iterator;
  typedef std::vector<unsigned>       SCCSet;

  Exp_SCCs_NoTiming(const Exp_PDG_NoTiming &pdg);
  void recompute(const Exp_PDG_NoTiming &pdg);
  void mod_recompute(Exp_PDG_NoTiming &pdg);

  // Enumerate strongly connected components in
  // reverse topological order.
  iterator begin() const { return sccs.begin(); }
  iterator end() const { return sccs.end(); }

  void setSequential(const SCC &scc);
  void setSequential(Vertices::ID v);
  bool mustBeInSequentialStage(const SCC &scc) const;
  bool mustBeInSequentialStage(Vertices::ID v) const;
  void print_scc_dot(raw_ostream &fout, unsigned sccnum) const;
  void print_replicated_scc_dot(raw_ostream &fout, unsigned sccnum, unsigned stage, bool first) const;
  void print_dot(raw_ostream &fout) const;
  void print_dot(const Exp_PDG_NoTiming &pdg, StringRef dotFileName, StringRef tredFileName) const;
  void getUpperBoundParallelStage(const Exp_PDG_NoTiming &pdg, std::vector<Instruction*> &insts) const;

  bool hasNonTrivialParallelStage(const Exp_PDG_NoTiming &pdg) const;

  // Does 'scc' have an edge to any of 'sccs'
  static bool hasEdge(const Exp_PDG_NoTiming &pdg, const SCC &scc, const SCCList &sccs);
  // Do any of 'sccs' have an edge to 'scc'
  static bool hasEdge(const Exp_PDG_NoTiming &pdg, const SCCList &sccs, const SCC &scc);
  // Does 'a' have an edge to 'b'
  static bool hasEdge(const Exp_PDG_NoTiming &pdg, const SCC &a, const SCC &b);

  unsigned size() const { return sccs.size(); }
  const SCC &get(unsigned i) const;

  unsigned numDoallSCCs() const;

  void computeReachabilityAmongSCCs(const Exp_PDG_NoTiming &pdg);

  bool orderedBefore(unsigned earlySCC, const SCCSet &lates) const;
  bool orderedBefore(const SCCSet &earlies, unsigned lateSCC) const;
  bool orderedBefore(unsigned earlySCC, unsigned lateSCC) const;

  static bool computeDagScc(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs);

  /// Don't use this; it's just for an experiment
  static bool computeDagScc_NoClient(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs);
  static bool computeDagScc_Dumb(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs);
  static bool computeDagScc_Dumb_OnlyCountNumQueries(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs);


  static bool computeDagScc_ModTarjan(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs);

  // Statistics
  bool abortTimeout;
  uint64_t abortStart;
  unsigned numRecomputeSCCs;

  void startWatchdog()
  {
    abortTimeout = false;
    abortStart = rdtsc();
  }

  bool checkWatchdog()
  {
    if( abortTimeout )
      return true;

    uint64_t now = rdtsc();
    if( now - abortStart > countCyclesPerSecond() * Exp_Timeout )
    {
      errs() << "TIMEOUT\n";
      abortTimeout = true;
    }

    return abortTimeout;
  }

  liberty::SpecPriv::SCCs toNormalSCCs();

private:
  typedef std::vector<int>          Vertex2Index;
  typedef std::vector<Vertices::ID> Stack;

  unsigned        index;
  Vertex2Index    idx, low;
  Stack           stack;

  SCCList         sccs;
  BitVector       inSequentialStage;

  bool            ordered_dirty;
  BitMatrix       ordered;

  void visit(Vertices::ID vertex, const PartialEdgeSet &G);
  void visit_mod(Vertices::ID vertex, Exp_PDG_NoTiming &pdg);
};

raw_ostream &operator<<(raw_ostream &fout, const Exp_SCCs_NoTiming &sccs);

}
}
}

#endif

