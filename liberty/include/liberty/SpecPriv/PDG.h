#ifndef LLVM_LIBERTY_SPEC_PRIV_PDG_H
#define LLVM_LIBERTY_SPEC_PRIV_PDG_H

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ControlSpeculation.h"

#include <vector>
#include <algorithm>

#include "llvm/Analysis/LoopInfo.h"

namespace liberty
{
struct PredictionSpeculation;
class Remediator;

enum DepType {
  Mem = 0,
  Ctrl = 1,
  Reg = 2
};

namespace SpecPriv
{
using namespace llvm;

class vset;

// The vertex set assign a unique (but arbitrary) id to every instruction
// within the loop.  Select these ids from a compact range [0,N).
// After construction, this set is immutable.
struct Vertices
{
  Vertices(Loop *loop);

  typedef unsigned ID;

  unsigned size() const { return map.size(); }

  Loop *getLoop() const { return loop; }

  // Forward map: O(log N)
  ID get(const Instruction *inst) const;

  // Reverse map: O(1)
  Instruction *get(ID idx) const;

  // Determine if the given instruction is included
  // O(log N)
  bool count(const Instruction *inst) const;

  void print_dot(raw_ostream &fout, ControlSpeculation *ctrlspec=0) const;

private:
  typedef std::vector<Instruction*> BiDiMap;

  Loop *loop;
  BiDiMap map;
};


raw_ostream &operator<<(raw_ostream &fout, const Vertices &v);



/// Represents partial knowledge of the program
/// dependence relationship between two vertices.
struct PartialEdge
{
  /// Is there a loop-carried control dependence?
  bool    lc_ctrl:1;

  /// Is there an intra-iteration control dependence?
  bool    ii_ctrl:1;

  /// Is there a loop-carried register data dependence?
  bool    lc_reg:1;

  /// Is there an intra-iteration register data dependence?
  bool    ii_reg:1;

  /// (if lc_mem_known==true), is there a loop-carried memory data depenence?
  bool    lc_mem:1;
  /// Do we know whether there is a loop-carried memory data dependence?
  bool    lc_mem_known:1;

  /// (if ii_mem_known==true), is there an intra-iteration memory data dependence?
  bool    ii_mem:1;
  /// Do we know whether there is an intra-iteration memory data dependence?
  bool    ii_mem_known:1;

  // Please don't add any more members, since this structure
  // fits nicely within one byte.

  bool isEdge() const;
  bool operator==(const PartialEdge &other) const;
  bool operator<(const PartialEdge &other) const;
  bool operator&(const PartialEdge &other) const;

  void print(raw_ostream &fout) const;

private:
  // Re-interpret cast this structure of packed-bits to
  // an integer.  This makes operator==, operator<
  // and operator& easier to implement.
  unsigned to_i() const;
};

raw_ostream &operator<<(raw_ostream &fout, const PartialEdge &pe);

struct AdjListIterator;

/// A word-size aggregate of several partial edge objects.
/// This is used inside PartialEdgeSet to make it more compact and
/// cache-friendly, and to reduce lookup times if there is any
/// locality in the keyspace.
struct WordSizePartialEdgeAggregate
{
  enum AggSize { AggregateSize = sizeof(unsigned) / sizeof(PartialEdge) };

  PartialEdge   elements[ AggregateSize ];
};



// A Program dependence graph for a set of N instructions.
// Represented as a partially known set of edges.
// Initially, E is /completely/ unknown.
// Mutators add knowledge, and potentially add edges too,
// i.e. absence of an edge is considered knowledge.
struct PartialEdgeSet
{
  PartialEdgeSet(const Vertices &v);

  // print stats
  void pstats(raw_ostream &fout) const;

  // Accessors ------------------------------------------------------

  unsigned numVertices() const { return V.size(); }
  unsigned size() const;

  void print(raw_ostream &fout) const;

  // Determine if a given vertex sources/sinks
  // a loop carried dep.
  bool hasLoopCarriedEdge(Vertices::ID v, Vertices::ID w) const;
  // Determine if we know whether or not there is a LC edge v->w
  bool knownLoopCarriedEdge(Vertices::ID v, Vertices::ID w) const;

  // Determine if there is an II edge v->w
  bool hasIntraIterationEdge(Vertices::ID v, Vertices::ID w) const;
  // Determine if we know whether or not there is an II edge v->w
  bool knownIntraIterationEdge(Vertices::ID v, Vertices::ID w) const;

  // Determine if there is any edge v->w
  bool hasEdge(Vertices::ID v, Vertices::ID w) const;
  bool hasEdge(Vertices::ID src, Vertices::ID dst, const PartialEdge &filter) const;

  bool hasLoopCarriedCtrlEdge(Vertices::ID v, Vertices::ID w) const;
  bool hasIntraIterationCtrlEdge(Vertices::ID v, Vertices::ID w) const;
  bool hasLoopCarriedRegEdge(Vertices::ID v, Vertices::ID w) const;
  bool hasIntraIterationRegEdge(Vertices::ID v, Vertices::ID w) const;
  bool hasLoopCarriedMemEdge(Vertices::ID v, Vertices::ID w) const;
  bool hasIntraIterationMemEdge(Vertices::ID v, Vertices::ID w) const;

  /// Iterate over successors of a vertex v.  Optionally,
  /// filter the class of edges visible through that iterator.
  typedef AdjListIterator iterator;
  iterator successor_begin(Vertices::ID) const;
  iterator successor_begin(Vertices::ID, const PartialEdge &filter) const;
  iterator successor_end(Vertices::ID v) const;

  // Mutators -------------------------------------------------------

  // Add control dependences
  void addIICtrl(Vertices::ID src, Vertices::ID dst);
  void addLCCtrl(Vertices::ID src, Vertices::ID dst);
  // Add register data dependences
  void addIIReg(Vertices::ID src, Vertices::ID dst);
  void addLCReg(Vertices::ID src, Vertices::ID dst);
  // Add memory data dependences
  void addIIMem(Vertices::ID src, Vertices::ID dst, bool present = true);
  void addLCMem(Vertices::ID src, Vertices::ID dst, bool present = true);

  // Remove dependence edges (satisfied by remediators)
  void removeLCMem(Vertices::ID src, Vertices::ID dst);
  void removeLCReg(Vertices::ID src, Vertices::ID dst);
  void removeLCCtrl(Vertices::ID src, Vertices::ID dst);
  void removeIIMem(Vertices::ID src, Vertices::ID dst);
  void removeIIReg(Vertices::ID src, Vertices::ID dst);
  void removeIICtrl(Vertices::ID src, Vertices::ID dst);

  // Query for a particular edge.
  const PartialEdge &find(Vertices::ID src, Vertices::ID dst) const;

  typedef std::pair<Vertices::ID, WordSizePartialEdgeAggregate> Adjacency;
  typedef std::vector<Adjacency> AdjacencyList;
  typedef std::vector<AdjacencyList> AdjacencyLists;

private:
  // Number of vertices
  const Vertices  & V;

  // Vertices::ID -> Vertices::ID -> PartialEdge
  AdjacencyLists adj_lists;

  PartialEdge &find(Vertices::ID src, Vertices::ID dst);
};

raw_ostream &operator<<(raw_ostream &fout, const PartialEdgeSet &e);

struct AdjListIterator
{
  AdjListIterator(const PartialEdgeSet::AdjacencyList::const_iterator &I,
                  const PartialEdgeSet::AdjacencyList::const_iterator &E,
                  const PartialEdge &F);
  AdjListIterator(const AdjListIterator &other);

  // prefix
  AdjListIterator &operator++();
  Vertices::ID operator*() const;

  bool operator!=(const AdjListIterator &other) const;

  void skipEmpty();

private:
  PartialEdgeSet::AdjacencyList::const_iterator I, E;
  unsigned offsetWithinAggregate;

  const PartialEdge filter;
};


/*
//sa8
struct PDGLoopNest
{
  PDGLoopNest(const PDG &pdg);

  //unsigned getNumLoops() const;
  //PDGLoop getLoop(unsigned i) const;
 // PDGNode *getOuterMostLatch() const;
  Vertices::ID getInnerMostLatch() const;

private:
  std::vector<Vertices:ID> latches;

};
*/


struct PDG
{
  /// Construct a complete PDG with all
  /// memory, control and register dependences.
  PDG(const Vertices &v, LoopAA *AA,
    ControlSpeculation &ctrlspec,
    PredictionSpeculation &predspec,
    const DataLayout *td = 0,
    bool ignoreAntiOutput = false,
    bool constrainSubLoops = false);

  /// Construct an initial PDG which contains
  /// ONLY Control and Register dependences.
  PDG(const Vertices &v,
    ControlSpeculation &ctrlspec,
    PredictionSpeculation &predspec,
    const DataLayout *td = 0,
    bool ignoreAntiOutput = false,
    bool constrainSubLoops = false);


  ~PDG();

  // print stats
  void pstats(raw_ostream &fout) const;

  unsigned numVertices() const { return V.size(); }

  /// Perform an INTRA-ITERATION memory dependence query, unless
  /// it has already been computed. Update the graph with new knowledge.
  /// Checks for intra-iteration reachability.
  /// If force==false, this will not perform the memory query if src and
  /// dst are already related by a control or register dependence.
  bool queryIntraIterationMemoryDep(Vertices::ID src, Vertices::ID dst, bool force = false);

  /// Perform a LOOP-CARRIED memory dependence query, unless
  /// it has already been computed. Update the graph with new knowledge.
  /// Assumes cross-iteration reachability.
  /// If force==false, this will not perform the memory query if src and
  /// dst are already related by a control or register dependence.
  bool queryLoopCarriedMemoryDep(Vertices::ID src, Vertices::ID dst, bool force = false);

  /// Determine if two vertices are ordered by a dependence.
  /// (either loop-carried or not)
  /// Do NOT perform any query; act only on the cache.
  bool hasEdge(Vertices::ID src, Vertices::ID dst) const;

  /// Determine if two vertices are ordered by a loop-carried dependence.
  /// Do NOT perform any query; act only on the cache.
  bool hasLoopCarriedEdge(Vertices::ID src, Vertices::ID dst) const;

  bool hasLoopCarriedCtrlEdge(Vertices::ID src, Vertices::ID dst) const;
  bool hasIntraIterationCtrlEdge(Vertices::ID src, Vertices::ID dst) const;
  bool hasLoopCarriedRegEdge(Vertices::ID src, Vertices::ID dst) const;
  bool hasIntraIterationRegEdge(Vertices::ID src, Vertices::ID dst) const;
  bool hasLoopCarriedMemEdge(Vertices::ID src, Vertices::ID dst) const;
  bool hasIntraIterationMemEdge(Vertices::ID src, Vertices::ID dst) const;

  // Dependence querying
  bool isDependent(Vertices::ID src, Vertices::ID dst) const;
  bool isDependent(Vertices::ID src, Vertices::ID dst, const PartialEdge &filter) const;
  bool isDependent(Vertices::ID src, Vertices::ID dst, const PartialEdge &filter,
      std::set< Vertices::ID > &visited) const;
  bool isIntraIterationDependent(Vertices::ID src, Vertices::ID dst) const;
  bool isLoopCarriedDependent(Vertices::ID src, Vertices::ID dst) const;
  bool isIntraIterationRegDependent(Vertices::ID src, Vertices::ID dst) const;
  bool isIntraIterationCtrlDependent(Vertices::ID src, Vertices::ID dst) const;
  bool isIntraIterationMemDependent(Vertices::ID src, Vertices::ID dst) const;
  bool isLoopCarriedRegDependent(Vertices::ID src, Vertices::ID dst) const;
  bool isLoopCarriedCtrlDependent(Vertices::ID src, Vertices::ID dst) const;
  bool isLoopCarriedMemDependent(Vertices::ID src, Vertices::ID dst) const;

  /// Build a cache to hold the transitive intra-tieration control dependence information
  void buildTransitiveIntraIterationControlDependenceCache(PartialEdgeSet& cache) const;

  /// Determine if two vertices are ordered by a transitive intra-iteration control dependece.
  bool hasTransitiveIntraIterationControlDependence(Vertices::ID src, Vertices::ID dst) const;

  /// Have we queried src->dst in the past?
  bool unknown(Vertices::ID src, Vertices::ID dst) const;

  /// Have we queried src->dst for loop-carried deps in the past?
  bool unknownLoopCarried(Vertices::ID src, Vertices::ID dst) const;

  // Can the remediators remove this loop carried edge?
  // if yes with what cost?
  int removableLoopCarriedEdge(Vertices::ID src, Vertices::ID dst) const;

  // A remediator was able to remove this dep. Remove it from the PDG
  void removeEdge(Vertices::ID src, Vertices::ID dst, bool lc, DepType dt);
  void removeLoopCarriedMemEdge(Vertices::ID src, Vertices::ID dst);
  void removeLoopCarriedCtrlEdge(Vertices::ID src, Vertices::ID dst);
  void removeLoopCarriedRegEdge(Vertices::ID src, Vertices::ID dst);
  void removeIntraIterationMemEdge(Vertices::ID src, Vertices::ID dst);
  void removeIntraIterationCtrlEdge(Vertices::ID src, Vertices::ID dst);
  void removeIntraIterationRegEdge(Vertices::ID src, Vertices::ID dst);

  void setRemediatedEdgeCost(int cost, Vertices::ID src, Vertices::ID dst,
                             bool lc, DepType dt);

  const Vertices &getV() const { return V; }
  const PartialEdgeSet &getE() const { return E; }

  //sot
  //const PDGLoopNest &getLN() const { return LN; }

  void setAA(LoopAA *AA) { aa = AA; }
  LoopAA *getAA() const { return aa; }

  void setRemed(Remediator *Remed) { remed = Remed; }
  Remediator *getRemed() const { return remed; }

  ControlSpeculation &getControlSpeculator() const { return ctrlspec; }

  // Statistics
  unsigned numQueries;

  // Number of complaints
  unsigned numComplaints;
  // Number of useful complaints satisfied
  unsigned numUsefulRemedies;
  // Number of complaints satisfied but not eventually needed
  unsigned numUnneededRemedies;


  // This is an evil constructor.  Try not to use it.  It exists only to
  // facilitate experiments, e.g. to convert a Exp_PDG_NoTiming, etc, into a
  // real PDG object.
  PDG(const Vertices &V, PartialEdgeSet &E, ControlSpeculation &ctrlspec, bool ignoreAO, LoopAA *aa);

  // copy constructor
  PDG(const PDG &pdg, const Vertices &v, ControlSpeculation &cs, bool ignoreAO);

private:
  const Vertices &V;
  PartialEdgeSet E;

  //sot (PDGLoopNest was deleted from PDG.cpp from Nick)
  //PDGLoopNest LN;

  // maps of edges to costs
  std::map<std::pair<Vertices::ID, Vertices::ID>, int>
      remediatedIIMemEdgesCostMap;
  std::map<std::pair<Vertices::ID, Vertices::ID>, int>
      remediatedLCMemEdgesCostMap;
  std::map<std::pair<Vertices::ID, Vertices::ID>, int>
      remediatedIICtrlEdgesCostMap;
  std::map<std::pair<Vertices::ID, Vertices::ID>, int>
      remediatedLCCtrlEdgesCostMap;
  std::map<std::pair<Vertices::ID, Vertices::ID>, int>
      remediatedIIRegEdgesCostMap;
  std::map<std::pair<Vertices::ID, Vertices::ID>, int>
      remediatedLCRegEdgesCostMap;

  ControlSpeculation &ctrlspec;

  LoopAA *aa;
  Remediator *remed;

  // Optionally ignore anti- and output-dependences
  // while construction DAG SCC
  const bool ignoreAntiOutput;

  void computeRegisterDeps(ControlSpeculation &ctrlspec, PredictionSpeculation &predspec);
  void computeControlDeps(ControlSpeculation &ctrlspec, const DataLayout *td);
  void computeExhaustivelyMemDeps();

  void constrainSubLoops();
  void constrainSubLoop(Loop *L);

  // Perform a raw, uncached pair of queries into the AA stack.
  bool queryMemoryDep(Vertices::ID src, Vertices::ID dst,
    LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV);

  // Perform a raw, uncached pair of queries into the AA stack.
  bool queryMemoryDep(Instruction *sop, Instruction *dop,
    LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV);


  LoopAA::ModRefResult query(Instruction *src, LoopAA::TemporalRelation rel, Instruction *dst, Loop *loop);
};

}
}

#endif
