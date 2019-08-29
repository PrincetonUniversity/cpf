#ifndef LLVM_LIBERTY_SPEC_PRIV_PDG_FAST_DAG_SCC_EXPERIMENT_NO_TIMING_H
#define LLVM_LIBERTY_SPEC_PRIV_PDG_FAST_DAG_SCC_EXPERIMENT_NO_TIMING_H

#include "liberty/Analysis/LoopAA.h"
#include "PDG.h"

#include <vector>
#include <algorithm>

#include "llvm/Analysis/LoopInfo.h"

namespace liberty
{
struct ControlSpeculation;
struct PredictionSpeculation;

namespace SpecPriv
{
struct PDG;

namespace FastDagSccExperiment
{

using namespace llvm;

struct Exp_PDG_NoTiming
{
  /// Construct an initial PDG which contains
  /// ONLY Control and Register dependences.
  Exp_PDG_NoTiming(const Vertices &v,
    ControlSpeculation &ctrlspec,
    PredictionSpeculation &predspec,
    const DataLayout *td = 0,
    bool ignoreAntiOutput = false);

  ~Exp_PDG_NoTiming();

  // print stats
  void pstats(raw_ostream &fout) const;

  unsigned numVertices() const { return V.size(); }

  /// Perform an INTRA-ITERATION memory dependence query, unless
  /// it has already been computed. Update the graph with new knowledge.
  /// Checks for intra-iteration reachability.
  /// If force==false, this will not perform the memory query if src and
  /// dst are already related by a control or register dependence.
  bool queryIntraIterationMemoryDep(Vertices::ID src, Vertices::ID dst, bool force = false);
  bool queryIntraIterationMemoryDep_OnlyCountNumQueries(Vertices::ID src, Vertices::ID dst, bool force = false);

  /// Perform a LOOP-CARRIED memory dependence query, unless
  /// it has already been computed. Update the graph with new knowledge.
  /// Assumes cross-iteration reachability.
  /// If force==false, this will not perform the memory query if src and
  /// dst are already related by a control or register dependence.
  bool queryLoopCarriedMemoryDep(Vertices::ID src, Vertices::ID dst, bool force = false);
  bool queryLoopCarriedMemoryDep_OnlyCountNumQueries(Vertices::ID src, Vertices::ID dst, bool force = false);

  /// Determine if two vertices are ordered by a dependence.
  /// (either loop-carried or not)
  /// Do NOT perform any query; act only on the cache.
  bool hasEdge(Vertices::ID src, Vertices::ID dst) const;

  /// Determine if two vertices are ordered by a loop-carried dependence.
  /// Do NOT perform any query; act only on the cache.
  bool hasLoopCarriedEdge(Vertices::ID src, Vertices::ID dst) const;

  /// Have we queried src->dst in the past?
  bool unknown(Vertices::ID src, Vertices::ID dst) const;

  /// Have we queried src->dst for loop-carried deps in the past?
  bool unknownLoopCarried(Vertices::ID src, Vertices::ID dst) const;

  /*
  // Can the remediators remove this loop carried edge?
  bool tryRemoveLoopCarriedMemEdge(Vertices::ID src, Vertices::ID dst);
  bool tryRemoveLoopCarriedCtrlEdge(Vertices::ID src, Vertices::ID dst);
  bool tryRemoveLoopCarriedRegEdge(Vertices::ID src, Vertices::ID dst);
  */

  // A remediator was able to remove this dep. Remove it from the PDG
  void removeLoopCarriedMemEdge(Vertices::ID src, Vertices::ID dst);
  void removeLoopCarriedCtrlEdge(Vertices::ID src, Vertices::ID dst);
  void removeLoopCarriedRegEdge(Vertices::ID src, Vertices::ID dst);

  const Vertices &getV() const { return V; }
  const PartialEdgeSet &getE() const { return E; }

  void setAA(LoopAA *AA) { aa = AA; }
  LoopAA *getAA() const { return aa; }

  /*
  void setRemed(Remediator *Remed) { remed = Remed; }
  Remediator *getRemed() const { return remed; }
  */

  ControlSpeculation &getControlSpeculator() const { return ctrlspec; }

  // Statistics
  // Number of times LoopAA::modref() was called
  unsigned numQueries;
  // Number of times LoopAA::modref() returned NoModRef
  unsigned numNoModRefQueries;
  // Number of times ::queryMemoryDep() was called
  unsigned numDepQueries;
  // Number of times ::queryMemoryDep() returned true
  unsigned numPositiveDepQueries;
  // Number of queries not performed because operations already ordered by a control dep.
  unsigned numQueriesSavedBecauseRedundantRegCtrl;

  /*
  // Number of complaints
  unsigned numComplaints;
  // Number of useful complaints satisfied
  unsigned numUsefulRemedies;
  // Number of complaints satisfied but not eventually needed
  unsigned numUnneededRemedies;
  */

  liberty::SpecPriv::PDG toNormalPDG();

private:
  const Vertices &V;
  PartialEdgeSet E;

  ControlSpeculation &ctrlspec;

  // Optionally ignore anti- and output-dependences
  // while construction DAG SCC
  const bool ignoreAntiOutput;

  LoopAA *aa;
  //Remediator *remed;

  void computeRegisterDeps(PredictionSpeculation &predspec);
  void computeControlDeps(ControlSpeculation &ctrlspec, const DataLayout *td);

  // Perform a raw, uncached pair of queries into the AA stack.
  bool queryMemoryDep(Vertices::ID src, Vertices::ID dst,
    LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV);

  // Perform a raw, uncached pair of queries into the AA stack.
  bool queryMemoryDep(Instruction *sop, Instruction *dop,
    LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV);

  bool queryMemoryDep_OnlyCountNumQueries(Vertices::ID src, Vertices::ID dst,
    LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV);
  bool queryMemoryDep_OnlyCountNumQueries(Instruction *sop, Instruction *dop,
    LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV);

  LoopAA::ModRefResult query(Instruction *src, LoopAA::TemporalRelation rel, Instruction *dst, Loop *loop);
  LoopAA::ModRefResult query_OnlyCountNumQueries(Instruction *src, LoopAA::TemporalRelation rel, Instruction *dst, Loop *loop);
};

}
}
}

#endif
