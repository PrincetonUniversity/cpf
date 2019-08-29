#define DEBUG_TYPE "dagscc"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/FileSystem.h"

#include "DAGSCC.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/Timer.h"
#include "liberty/Utilities/Tred.h"

//#include "Classify.h"
#include "Exp_PDG_NoTiming.h"
#include "Exp_DAGSCC_NoTiming.h"

#include <iterator>
#include <set>

namespace liberty
{
namespace SpecPriv
{
namespace FastDagSccExperiment
{
using namespace llvm;

bool Exp_SCCs_NoTiming::hasEdge(const Exp_PDG_NoTiming &pdg, const Exp_SCCs_NoTiming::SCC &scc, const Exp_SCCs_NoTiming::SCCList &sccs)
{
  for(Exp_SCCs_NoTiming::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
    if( hasEdge(pdg, scc, *i) )
      return true;

  return false;
}

bool Exp_SCCs_NoTiming::hasEdge(const Exp_PDG_NoTiming &pdg, const Exp_SCCs_NoTiming::SCCList &sccs, const Exp_SCCs_NoTiming::SCC &scc)
{
  for(Exp_SCCs_NoTiming::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
    if( hasEdge(pdg, *i, scc) )
      return true;

  return false;
}

bool Exp_SCCs_NoTiming::hasEdge(const Exp_PDG_NoTiming &pdg, const Exp_SCCs_NoTiming::SCC &a, const Exp_SCCs_NoTiming::SCC &b)
{
  for(SCC::const_iterator i=a.begin(), e=a.end(); i!=e; ++i)
  {
    Vertices::ID A = *i;

    for(SCC::const_iterator j=b.begin(), z=b.end(); j!=z; ++j)
    {
      Vertices::ID B = *j;

      if( pdg.hasEdge(A,B) )
        return true;
    }
  }

  return false;
}

const Exp_SCCs_NoTiming::SCC &Exp_SCCs_NoTiming::get(unsigned i) const
{
  return sccs[i];
}

bool Exp_SCCs_NoTiming::orderedBefore(unsigned earlySCC, const SCCSet &lates) const
{
  for(SCCSet::const_iterator i=lates.begin(), e=lates.end(); i!=e; ++i)
    if( orderedBefore(earlySCC,*i) )
      return true;
  return false;
}

bool Exp_SCCs_NoTiming::orderedBefore(const SCCSet &earlies, unsigned lateSCC) const
{
  for(SCCSet::const_iterator i=earlies.begin(), e=earlies.end(); i!=e; ++i)
    if( orderedBefore(*i,lateSCC) )
      return true;
  return false;
}

bool Exp_SCCs_NoTiming::orderedBefore(unsigned earlySCC, unsigned lateSCC) const
{
  assert( !ordered_dirty && "Must run computeReachabilityAmongExp_SCCs_NoTiming() first");
  return ordered.test(earlySCC,lateSCC);
}


Exp_SCCs_NoTiming::Exp_SCCs_NoTiming(const Exp_PDG_NoTiming &pdg)
  : abortTimeout(false),
    abortStart(0),
    numRecomputeSCCs(0),
    index(0),
    idx(),
    low(),
    stack(),
    sccs(),
    inSequentialStage( pdg.numVertices() ),
    ordered_dirty(true),
    ordered(1)
{}

void Exp_SCCs_NoTiming::recompute(const Exp_PDG_NoTiming &pdg)
{
  ++numRecomputeSCCs;

  const PartialEdgeSet &G = pdg.getE();
  const unsigned N = pdg.numVertices();

  DEBUG(errs() << "(Re-)computing Exp_SCCs_NoTiming on graph of " << N << " vertices\n");

  TIME("Compute Exp_SCCs_NoTiming",
  index = 0;
  idx.insert( idx.end(), N, -1 );
  low.insert( low.end(), N,  0 );
  sccs.clear();
  inSequentialStage.reset();

  for(Vertices::ID i=0; i<N; ++i)
    if( -1 == idx[i] )
      visit(i,G);

  idx.clear();
  low.clear();
  stack.clear();
  );

  ordered_dirty = true;
  DEBUG(errs() << "Done (re-)computing Exp_SCCs_NoTiming => " << sccs.size() << '\n');
}

void Exp_SCCs_NoTiming::computeReachabilityAmongSCCs(const Exp_PDG_NoTiming &pdg)
{
  const unsigned N_scc = sccs.size();

  TIME("Compute the 'ordered' relation",
  ordered.resize( N_scc );
  // sccs is in reverse topological order
  for(unsigned l=0; l<N_scc; ++l)
  {
    const SCC &late = sccs[l];
    for(unsigned e=l+1; e<N_scc; ++e)
    {
      const SCC &early = sccs[e];
      if( hasEdge(pdg,early,late) )
        ordered.set(e,l);
    }
  }
  ordered.transitive_closure();
  );

  ordered_dirty = false;
}

void Exp_SCCs_NoTiming::setSequential(const SCC &scc)
{
  for(SCC::const_iterator i=scc.begin(), e=scc.end(); i!=e; ++i)
    setSequential(*i);
}

void Exp_SCCs_NoTiming::setSequential(Vertices::ID v)
{
  inSequentialStage.set(v);
}

bool Exp_SCCs_NoTiming::mustBeInSequentialStage(const SCC &scc) const
{
  return mustBeInSequentialStage( scc.front() );
}

bool Exp_SCCs_NoTiming::mustBeInSequentialStage(Vertices::ID v) const
{
  return inSequentialStage.test(v);
}

void Exp_SCCs_NoTiming::print_dot(const Exp_PDG_NoTiming &pdg, StringRef dot, StringRef tred) const
{
  {
    std::error_code ec;
    raw_fd_ostream fout(dot, ec, sys::fs::F_RW);

    fout << "digraph \"PDG\" {\n" << pdg.getV() << *this << pdg.getE() << "}\n";
  }

  runTred(dot.data(),tred.data());
}

void Exp_SCCs_NoTiming::print_dot(raw_ostream &fout) const
{
  for(unsigned i=0; i<sccs.size(); ++i)
    print_scc_dot(fout,i);
}

void Exp_SCCs_NoTiming::print_scc_dot(raw_ostream &fout, unsigned i) const
{
  fout << "subgraph cluster_SCC" << i << " {\n";
  const SCC &scc = sccs[i];

  if( mustBeInSequentialStage(scc) )
    fout << "  style=filled;\ncolor=red;\nlabel=\"S " << i << "\"\n";
  else
    fout << "  style=filled;\ncolor=darkgreen;\nlabel=\"P " << i << "\"\n";

  for(unsigned j=0; j<scc.size(); ++j)
  {
    unsigned op = scc[j];
    fout << "  n" << op << ";\n";
  }

  fout << "}\n";
}

void Exp_SCCs_NoTiming::print_replicated_scc_dot(raw_ostream &fout, unsigned sccno, unsigned stageno, bool first) const
{
  fout << "subgraph cluster_SCC" << sccno << "_rep" << stageno << " {\n";
  const SCC &scc = sccs[sccno];

  fout << "  style=filled;\ncolor=orange;\nlabel=\"rep " << sccno << "\"\n";

  if( first )
    for(unsigned j=0; j<scc.size(); ++j)
    {
      unsigned op = scc[j];
      fout << "  n" << op << ";\n";
    }

  fout << "}\n";
}

void Exp_SCCs_NoTiming::getUpperBoundParallelStage(const Exp_PDG_NoTiming &pdg, std::vector<Instruction*> &insts) const
{
  for(Exp_SCCs_NoTiming::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCC &scc = *i;
    if( mustBeInSequentialStage(scc) )
      continue;

    for(SCC::const_iterator j=scc.begin(), z=scc.end(); j!=z; ++j)
    {
      Vertices::ID v = *j;
      Instruction *inst = pdg.getV().get(v);

      insts.push_back(inst);
    }
  }
}

bool Exp_SCCs_NoTiming::hasNonTrivialParallelStage(const Exp_PDG_NoTiming &pdg) const
{
  ControlSpeculation &cspec = pdg.getControlSpeculator();

  for(Exp_SCCs_NoTiming::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCC &scc = *i;
    if( mustBeInSequentialStage(scc) )
      continue;

    // Is the parallel stage substantial?
    for(SCC::const_iterator j=scc.begin(), z=scc.end(); j!=z; ++j)
    {
      Vertices::ID v = *j;
      Instruction *inst = pdg.getV().get(v);

/*
      // PHIs and branches don't really carry execution weight
      if( isa<PHINode>(inst) )
        continue;
      if( isa<BranchInst>(inst) )
        continue;
      if( isa<SwitchInst>(inst) )
        continue;
*/
      if( ! inst->mayReadOrWriteMemory() )
        continue;

      // Speculative assumption: dead instructions
      // will not execute, thus they carry no weight.
      if( cspec.isSpeculativelyDead(inst) )
        continue;

      return true;
    }
  }

  DEBUG(errs() << "  Has no non-trivial parallel stage.\n");
  return false;
}

void Exp_SCCs_NoTiming::visit(Vertices::ID vertex, const PartialEdgeSet &G)
{
  idx[vertex] = index;
  low[vertex] = index;
  ++index;

  stack.push_back(vertex);

  // Foreach successor 'succ' of 'vertex'
  for(PartialEdgeSet::iterator i = G.successor_begin(vertex), e=G.successor_end(vertex); i!=e; ++i)
  {
    const Vertices::ID succ = *i;

    // Is this the first path to reach 'succ' ?
    if( -1 == idx[succ] )
    {
      visit(succ,G);
      low[vertex] = std::min( low[vertex], low[succ] );
    }

    // Not the first path.  Is 'succ' on this path?
    else if( std::find(stack.begin(), stack.end(), succ) != stack.end() )
    {
      low[vertex] = std::min( low[vertex], idx[succ] );
    }
  }

  // Is vertex the root of an SCC?
  if( idx[vertex] == low[vertex] )
  {
    // Copy this SCC into the result,
    // and determine if it is a parallel SCC.
    sccs.push_back(SCC());
    SCC &scc = sccs.back();

    for(;;)
    {
      Vertices::ID v = stack.back();
      stack.pop_back();

      scc.push_back(v);


      if( v == vertex )
        break;
    }

    bool isSequential = false;
    for(SCC::const_iterator i=scc.begin(), e=scc.end(); i!=e; ++i)
    {
      Vertices::ID v = *i;

      for(SCC::const_iterator j=scc.begin(); j!=e; ++j)
      {
        Vertices::ID w = *j;

        if( G.hasLoopCarriedEdge(v,w) )
        {
          isSequential = true;
          break;
        }
      }

      if( isSequential )
        break;
    }

    if( isSequential )
      setSequential(scc);
  }
}

raw_ostream &operator<<(raw_ostream &fout, const Exp_SCCs_NoTiming &sccs)
{
  sccs.print_dot(fout);
  return fout;
}

static void excludeLoopCarriedReflexiveDeps(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  DEBUG(errs() << "Begin excludeLoopCarriedReflexiveDeps()\n");
  unsigned numNewEdges=0;
  for(Exp_SCCs_NoTiming::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const Exp_SCCs_NoTiming::SCC &scc = *i;
    if( sccs.mustBeInSequentialStage(scc) )
      continue;

    for(Exp_SCCs_NoTiming::SCC::const_iterator j=scc.begin(), z=scc.end(); j!=z; ++j)
    {
      Vertices::ID v = *j;
      if( pdg.queryLoopCarriedMemoryDep(v,v) )
      {
//        DEBUG(errs() << "  Found loop-carried mem dep on " << *pdg.getV().get(v) << '\n');
        ++numNewEdges;
        sccs.setSequential(scc);
        break;
      }
    }
  }
  DEBUG(errs() << "Done excludeLoopCarriedReflexiveDeps(): +" << numNewEdges << '\n');
}

static void excludeLoopCarriedDepsWithinSCC(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  DEBUG(errs() << "Begin excludeLoopCarriedDepsWithinSCC()\n");
  unsigned numNewEdges=0;

  const Vertices &V = pdg.getV();

  for(Exp_SCCs_NoTiming::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const Exp_SCCs_NoTiming::SCC &scc = *i;
    if( sccs.mustBeInSequentialStage(scc) )
      continue;

    for(Exp_SCCs_NoTiming::SCC::const_iterator j=scc.begin(), z=scc.end(); j!=z; ++j)
    {
      Vertices::ID v = *j;
      Instruction *iv = V.get(v);
      if( ! iv->mayReadOrWriteMemory() )
        continue;

      bool killed = false;
      for(Exp_SCCs_NoTiming::SCC::const_iterator k=scc.begin(); k!=z; ++k)
      {
        Vertices::ID w = *k;

        if( pdg.queryLoopCarriedMemoryDep(v,w) )
        {
//          DEBUG(errs() << "  Found loop-carried mem dep in scc from " << *pdg.getV().get(v) << " to " << *pdg.getV().get(w) << '\n');
          sccs.setSequential(scc);
          ++numNewEdges;
          killed = true;
          break;
        }
      }

      if( killed )
        break;
    }
  }
  DEBUG(errs() << "Done excludeLoopCarriedDepsWithinSCC(): +" << numNewEdges << '\n');
}

static bool queryLoopCarriedMemoryDep(Exp_PDG_NoTiming &pdg, const Exp_SCCs_NoTiming::SCC &sources, const Exp_SCCs_NoTiming::SCC &dests)
{
  const Vertices &V = pdg.getV();

  for(Exp_SCCs_NoTiming::SCC::const_iterator i=sources.begin(), e=sources.end(); i!=e; ++i)
  {
    Vertices::ID src = *i;
    Instruction *isrc = V.get(src);

    if( !isrc->mayReadOrWriteMemory() )
      continue;

    for(Exp_SCCs_NoTiming::SCC::const_iterator j=dests.begin(), z=dests.end(); j!=z; ++j)
    {
      Vertices::ID dst = *j;

      if( pdg.queryLoopCarriedMemoryDep(src,dst) )
      {
        Instruction *idst = V.get(dst);
        DEBUG(errs() << "Found LC mem dep from " << *isrc  << " to " << *idst << '\n');
        return true;
      }
    }
  }

  return false;
}

static bool queryIntraIterationMemoryDep(Exp_PDG_NoTiming &pdg, const Exp_SCCs_NoTiming::SCC &sources, const Exp_SCCs_NoTiming::SCC &dests)
{
  const Vertices &V = pdg.getV();

  for(Exp_SCCs_NoTiming::SCC::const_iterator i=sources.begin(), e=sources.end(); i!=e; ++i)
  {
    Vertices::ID src = *i;
    Instruction *isrc = V.get(src);

    if( !isrc->mayReadOrWriteMemory() )
      continue;

    for(Exp_SCCs_NoTiming::SCC::const_iterator j=dests.begin(), z=dests.end(); j!=z; ++j)
    {
      Vertices::ID dst = *j;

      if( pdg.queryIntraIterationMemoryDep(src,dst) )
      {
//        Instruction *idst = V.get(dst);
//        DEBUG(errs() << "Found II ATG mem dep from " << *isrc  << " to " << *idst << '\n');
        return true;
      }
    }
  }

  return false;
}

static bool queryUnrelatedSCCsWithTheGrain(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  DEBUG(errs() << "Begin queryUnrelatedSCCsWithTheGrain()\n");
  bool mod = false;
  unsigned numNewEdges=0;

  // This list is in REVERSE topological order.
  for(unsigned l=0, N=sccs.size(); l<N; ++l)
  {
    DEBUG(errs() << "- from SCC #" << l << '\n');
    const Exp_SCCs_NoTiming::SCC &later = sccs.get(l);

    if( later.size() == 1 )
    {
      // A common case: a singleton SCC which contains no memory
      // operations.  Short circuit here to skip a lot of iteration...
      Instruction *inst = pdg.getV().get( later.front() );
      if( !inst->mayReadOrWriteMemory() )
        continue;
    }

    // This list is in REVERSE topological order.
    for(unsigned e=l+1; e<N; ++e)
    {
      const Exp_SCCs_NoTiming::SCC &earlier = sccs.get(e);

      // Either (earlier) is ordered-before (later)
      //     or they are un-ordered.

      // Only do this query if these two SCCs are un-ordered
      if( sccs.hasEdge(pdg, earlier, later) )
        continue;

      if( queryLoopCarriedMemoryDep(pdg, earlier, later)
      ||  queryIntraIterationMemoryDep(pdg, earlier, later) )
      {
        ++numNewEdges;
        mod = true;
      }

      if( sccs.checkWatchdog() )
        return true;
    }
  }

  DEBUG(errs() << "Done queryUnrelatedSCCsWithTheGrain(): +" << numNewEdges <<'\n');
  return mod;
}



static bool queryAgainstTheGrain(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  DEBUG(errs() << "Begin queryAgainstTheGrain()\n");
  bool mod = false;
  unsigned numNewEdges=0;

  // This list is in REVERSE topological order.
  for(Exp_SCCs_NoTiming::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const Exp_SCCs_NoTiming::SCC &later = *i;

    if( later.size() == 1 )
    {
      // A common case: a singleton SCC which contains no memory
      // operations.  Short circuit here to skip a lot of iteration...
      Instruction *inst = pdg.getV().get( later.front() );
      if( !inst->mayReadOrWriteMemory() )
        continue;
    }

    // This list is in REVERSE topological order.
    for(Exp_SCCs_NoTiming::iterator j=i+1; j!=e; ++j)
    {
      assert( i != j );
      const Exp_SCCs_NoTiming::SCC &earlier = *j;

      // SCCs lists the strongly connected components
      // in reverse topological order.
      // Thus, either earlier ->* later or
      // they are incomparable.

      // Look for loop-carried deps from any
      // operation in earlier to any operation
      // in later.

      if( queryLoopCarriedMemoryDep(pdg, later, earlier)
      ||  queryIntraIterationMemoryDep(pdg, later, earlier) )
      {
        ++numNewEdges;
        mod = true;
        break;
      }

      if( sccs.checkWatchdog() )
        return true;
    }
  }

  DEBUG(errs() << "Done queryAgainstTheGrain(): +" << numNewEdges << '\n');
  return mod;
}


static void queryLoopCarriedMemoryDepsWithinParallelStage(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  DEBUG(errs() << "Begin queryLoopCarriedMemoryDepsWithinParallelStage()\n");
  unsigned numNewEdges=0;

  // For each parallel scc
  for(Exp_SCCs_NoTiming::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const Exp_SCCs_NoTiming::SCC &later = *i;

    if( later.size() == 1 )
    {
      // A common case: a singleton SCC which contains no memory
      // operations.  Short circuit here to skip a lot of iteration...
      Instruction *inst = pdg.getV().get( later.front() );
      if( !inst->mayReadOrWriteMemory() )
        continue;
    }

    if( sccs.mustBeInSequentialStage(later) )
      continue;

    // To each later parallel scc
    for(Exp_SCCs_NoTiming::iterator j=i+1; j!=e; ++j)
    {
      assert( i != j );
      const Exp_SCCs_NoTiming::SCC &earlier = *j;

      if( sccs.mustBeInSequentialStage(earlier) )
        continue;

      if( queryLoopCarriedMemoryDep(pdg, earlier, later) )
      {
        ++numNewEdges;
        break;
      }
    }
  }
  DEBUG(errs() << "Done queryLoopCarriedMemoryDepsWithinParallelStage(): +" << numNewEdges << '\n');
}




// Basically, this function performs enough AA queries
// to accuratelly know the structure of the DAG-SCC.
// By carefully choosing which queries to perform, we
// end up performing (on average) about 25% of the
// queries needed to compute the PDG.
//
// You don't need to trust this; instead, see if you
// trust Pipeline::assertPipelineProperty().
bool Exp_SCCs_NoTiming::computeDagScc(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  pdg.numQueries = 0;
  sccs.numRecomputeSCCs = 0;
  sccs.startWatchdog();

  // ------------------- SCC guide further analysis
  sccs.recompute(pdg);

  if( sccs.checkWatchdog() ) return true;
  // How big is the parallel stage?
  if( ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;

  // ------------------- Memory dependences are EXPENSIVE

  // Try to exclude SCCs from the parallel stage
  // via the loop-carried reflex-edge test.
  // This test /cannot/ change the SCCs, but will mark
  // some SCCs as sequential.

  excludeLoopCarriedReflexiveDeps(pdg,sccs);

  if( sccs.checkWatchdog() ) return true;
  // How big is the parallel stage?
  if( ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;

  // Try to exclude SCCs from the parallel stage
  // via the loop-carried within-scc test
  // This test /cannot/ change the SCCs, but will mark
  // some SCCs as sequential.

  excludeLoopCarriedDepsWithinSCC(pdg,sccs);


  if( sccs.checkWatchdog() ) return true;
  // How big is the parallel stage?
  if( ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;

  // sccs lists SCCs in a topological sort of
  // a partial order.  Sometimes, A precedes B
  // yet there is no known dependence from A to B.
  // Remedy that.

//  if( queryUnrelatedSCCsWithTheGrain(pdg,sccs) )
//  {
//    if( sccs.checkWatchdog() ) return true;
//    sccs.recompute(pdg);
//  }
  queryUnrelatedSCCsWithTheGrain(pdg,sccs);
  if( sccs.checkWatchdog() ) return true;


  // Try to merge SCCs via loop-carried cross-scc test.
  // This /can/ change our SCCs, causing them to merge.
  while( queryAgainstTheGrain(pdg,sccs) )
  {
    sccs.recompute(pdg);

    if( sccs.checkWatchdog() ) return true;
    // How big is the parallel stage?
    if( ! sccs.hasNonTrivialParallelStage(pdg) )
      return false;

    queryUnrelatedSCCsWithTheGrain(pdg,sccs);
    if( sccs.checkWatchdog() ) return true;
  }

  // Query with-the-grain across-sccs
  // consider only parallel SCCs.
  queryLoopCarriedMemoryDepsWithinParallelStage(pdg,sccs);

  if( sccs.checkWatchdog() ) return true;
  // How big is the parallel stage?
  if( ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;

/*
  // Query across-sccs, loop-carried
  // deps from a parallel scc to any other scc
  queryLoopCarriedMemoryDepsFromParallelStage(pdg,sccs);

  // How big is the parallel stage?
  if( ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;
*/

  excludeLoopCarriedDepsWithinSCC(pdg,sccs);

  if( sccs.checkWatchdog() ) return true;
  // How big is the parallel stage?
  if( ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;


  return true;
}

bool Exp_SCCs_NoTiming::computeDagScc_NoClient(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  sccs.startWatchdog();
  pdg.numQueries = 0;
  sccs.numRecomputeSCCs = 0;

  sccs.recompute(pdg);
  // ------------------- Memory dependences are EXPENSIVE

  // sccs lists SCCs in a topological sort of
  // a partial order.  Sometimes, A precedes B
  // yet there is no known dependence from A to B.
  // Remedy that.

//  if( queryUnrelatedSCCsWithTheGrain(pdg,sccs) )
//  {
//    if( sccs.checkWatchdog() ) return true;
//    sccs.recompute(pdg);
//  }
  queryUnrelatedSCCsWithTheGrain(pdg,sccs);
  if( sccs.checkWatchdog() ) return true;

  // Try to merge SCCs via loop-carried cross-scc test.
  // This /can/ change our SCCs, causing them to merge.
  while( queryAgainstTheGrain(pdg,sccs) )
  {
    if( sccs.checkWatchdog() ) return true;
    sccs.recompute(pdg);

    queryUnrelatedSCCsWithTheGrain(pdg,sccs);
    if( sccs.checkWatchdog() ) return true;
  }

/*
  // Query with-the-grain across-sccs
  // consider only parallel SCCs.
  queryLoopCarriedMemoryDepsWithinParallelStage(pdg,sccs);
*/
  return true;
}

bool Exp_SCCs_NoTiming::computeDagScc_Dumb(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  pdg.numQueries = 0;
  sccs.numRecomputeSCCs = 0;
  sccs.startWatchdog();

  const Vertices &V = pdg.getV();
  const unsigned N=V.size();

  for(unsigned i=0; i<N; ++i)
  {
    Instruction *ii = V.get(i);
    if( ! ii->mayReadOrWriteMemory() )
      continue;

    for(unsigned j=0; j<N; ++j)
    {
      pdg.queryLoopCarriedMemoryDep(i,j,true);
      pdg.queryIntraIterationMemoryDep(i,j,true);

      if( sccs.checkWatchdog() )
        return true;
    }
  }

  sccs.recompute(pdg);
  return true;
}

bool Exp_SCCs_NoTiming::computeDagScc_ModTarjan(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  pdg.numQueries = 0;
  sccs.numRecomputeSCCs = 0;
  sccs.startWatchdog();

  sccs.mod_recompute(pdg);
  return true;
}
void Exp_SCCs_NoTiming::mod_recompute(Exp_PDG_NoTiming &pdg)
{
  ++numRecomputeSCCs;

  const unsigned N = pdg.numVertices();

  index = 0;
  idx.insert( idx.end(), N, -1 );
  low.insert( low.end(), N,  0 );
  sccs.clear();
  inSequentialStage.reset();

  for(Vertices::ID i=0; i<N; ++i)
    if( -1 == idx[i] )
      visit_mod(i,pdg);

  idx.clear();
  low.clear();
  stack.clear();

  ordered_dirty = true;
}

void Exp_SCCs_NoTiming::visit_mod(Vertices::ID vertex, Exp_PDG_NoTiming &pdg)
{
  if( checkWatchdog() )
    return;

  idx[vertex] = index;
  low[vertex] = index;
  ++index;

  stack.push_back(vertex);

  // We split the 'foreach successor' loop into two loops:
  // First, visiting known successors;
  // Then, visit those successors which will potentially change the SCC.

  // Foreach known successor:
  const PartialEdgeSet &edgeset = pdg.getE();
  std::vector<Vertices::ID> exclude;
  for(PartialEdgeSet::iterator i = edgeset.successor_begin(vertex), e=edgeset.successor_end(vertex); i!=e; ++i)
  {
    const Vertices::ID succ = *i;
    exclude.push_back(succ);

    // Is this the first path to reach 'succ' ?
    if( -1 == idx[succ] )
    {
      visit_mod(succ,pdg);
      low[vertex] = std::min( low[vertex], low[succ] );
    }

    // Not the first path.  Is 'succ' on this path?
    else if( std::find(stack.begin(), stack.end(), succ) != stack.end() )
    {
      low[vertex] = std::min( low[vertex], idx[succ] );
    }
  }
  // Foreach unknown successor:
  const unsigned N = pdg.getV().size();
  for(Vertices::ID succ=0; succ<N; ++succ)
  {
    if( checkWatchdog() )
      return;

    // Don't re-visit the successors which we already visited
    // in the first loop.
    if( std::find( exclude.begin(), exclude.end(), succ ) != exclude.end() )
      continue; // was already visited by first loop

    // Before checking if the edge exists,
    // determine if the presence/absence of the edge will
    // affect components.

    // Either (i) idx[succ] is undefined (-1); or
    //       (ii) succ is not on the stack, or
    //      (iii) succ is on the stack, and idx[succ] < low[vertex]
    const bool isOnStack = std::find(stack.begin(), stack.end(), succ) != stack.end();
    if( -1 == idx[succ] )
    { /* If the edge exists, it will cause us to recur at POINT X, below*/ }
    else if( isOnStack )
    {
      if( idx[succ] < low[vertex] )
      { /* If this edge exists, it will change low[vertex] at point Y, below */ }
      else
        continue; // skip it; it cannot affect components.
    }
    else
      continue; // skip it; it cannot affect components.

    // Determine if there is an edge vertex->succ
    if( pdg.queryLoopCarriedMemoryDep(vertex,succ)
    ||  pdg.queryIntraIterationMemoryDep(vertex,succ) )
    { /* Yes, this edge exists */ }
    else
      continue; // No, there is no edge vertex->succ.

    // Is this the first path to reach 'succ' ?
    if( -1 == idx[succ] )
    {
      // POINT X
      visit_mod(succ,pdg);
      low[vertex] = std::min( low[vertex], low[succ] );
    }

    // Not the first path.  Is 'succ' on this path?
    else if( isOnStack )
    {
      // POINT Y
      low[vertex] = std::min( low[vertex], idx[succ] );
    }
  }

  // Is vertex the root of an SCC?
  if( idx[vertex] == low[vertex] )
  {
    // Copy this SCC into the result,
    // and determine if it is a parallel SCC.
    sccs.push_back(SCC());
    SCC &scc = sccs.back();

    for(;;)
    {
      Vertices::ID v = stack.back();
      stack.pop_back();

      scc.push_back(v);


      if( v == vertex )
        break;
    }

    bool isSequential = false;
    for(SCC::const_iterator i=scc.begin(), e=scc.end(); i!=e; ++i)
    {
      Vertices::ID v = *i;

      for(SCC::const_iterator j=scc.begin(); j!=e; ++j)
      {
        Vertices::ID w = *j;

        if( pdg.hasLoopCarriedEdge(v,w) )
        {
          isSequential = true;
          break;
        }
      }

      if( isSequential )
        break;
    }

    if( isSequential )
      setSequential(scc);
  }
}



bool Exp_SCCs_NoTiming::computeDagScc_Dumb_OnlyCountNumQueries(Exp_PDG_NoTiming &pdg, Exp_SCCs_NoTiming &sccs)
{
  pdg.numQueries = 0;
  sccs.numRecomputeSCCs = 0;

  const Vertices &V = pdg.getV();
  const unsigned N=V.size();

  for(unsigned i=0; i<N; ++i)
  {
    Instruction *ii = V.get(i);
    if( ! ii->mayReadOrWriteMemory() )
      continue;

    for(unsigned j=0; j<N; ++j)
    {
      pdg.queryLoopCarriedMemoryDep_OnlyCountNumQueries(i,j,true);
      pdg.queryIntraIterationMemoryDep_OnlyCountNumQueries(i,j,true);
    }
  }

  return true;
}

liberty::SpecPriv::SCCs Exp_SCCs_NoTiming::toNormalSCCs()
{
  return liberty::SpecPriv::SCCs( sccs, inSequentialStage );
}

unsigned Exp_SCCs_NoTiming::numDoallSCCs() const
{
  unsigned count = 0;
  for(iterator i=begin(), e=end(); i!=e; ++i)
  {
    const SCC &scc = *i;
    if( ! mustBeInSequentialStage(scc) )
      ++count;
  }
  return count;

}

}
}
}

