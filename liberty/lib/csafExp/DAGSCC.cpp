#define DEBUG_TYPE "dagscc"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/FileSystem.h"

#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/Timer.h"
#include "liberty/Utilities/Tred.h"

//#include "Classify.h"
#include "DAGSCC.h"
#include "PDG.h"

#include <iterator>
#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

bool SCCs::hasEdge(const PDG &pdg, const SCCs::SCC &scc, const SCCs::SCCList &sccs)
{
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
    if( hasEdge(pdg, scc, *i) )
      return true;

  return false;
}

bool SCCs::hasEdge(const PDG &pdg, const SCCs::SCCList &sccs, const SCCs::SCC &scc)
{
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
    if( hasEdge(pdg, *i, scc) )
      return true;

  return false;
}

bool SCCs::hasEdge(const PDG &pdg, const SCCs::SCC &a, const SCCs::SCC &b)
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

const SCCs::SCC &SCCs::get(unsigned i) const
{
  return sccs[i];
}

bool SCCs::orderedBefore(unsigned earlySCC, const SCCSet &lates) const
{
  for(SCCSet::const_iterator i=lates.begin(), e=lates.end(); i!=e; ++i)
    if( orderedBefore(earlySCC,*i) )
      return true;
  return false;
}

bool SCCs::orderedBefore(const SCCSet &earlies, unsigned lateSCC) const
{
  for(SCCSet::const_iterator i=earlies.begin(), e=earlies.end(); i!=e; ++i)
    if( orderedBefore(*i,lateSCC) )
      return true;
  return false;
}

bool SCCs::orderedBefore(unsigned earlySCC, unsigned lateSCC) const
{
  assert( !ordered_dirty && "Must run computeReachabilityAmongSCCs() first");
  return ordered.test(earlySCC,lateSCC);
}

// Evil constructor
SCCs::SCCs(SCCList &scclist, BitVector &inSeq)
  : index(0), idx(), low(), stack(), sccs(), inSequentialStage(), ordered_dirty(true), ordered(1)
{
  sccs.swap(scclist);
  inSequentialStage.swap(inSeq);
}

SCCs::SCCs(const PDG &pdg)
  : index(0),
    idx(),
    low(),
    stack(),
    sccs(),
    inSequentialStage( pdg.numVertices() ),
    ordered_dirty(true),
    ordered(1)
{}

void SCCs::recompute(const PDG &pdg)
{
  const PartialEdgeSet &G = pdg.getE();
  const unsigned N = pdg.numVertices();

  DEBUG(errs() << "(Re-)computing SCCs on graph of " << N << " vertices\n");

  TIME("Compute SCCs",
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
  DEBUG(errs() << "Done (re-)computing SCCs => " << sccs.size() << '\n');
}

void SCCs::computeReachabilityAmongSCCs(const PDG &pdg)
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

void SCCs::setSequential(const SCC &scc)
{
  for(SCC::const_iterator i=scc.begin(), e=scc.end(); i!=e; ++i)
    setSequential(*i);
}

void SCCs::setSequential(Vertices::ID v)
{
  inSequentialStage.set(v);
}

void SCCs::setParallel(const SCC &scc)
{
  for(SCC::const_iterator i=scc.begin(), e=scc.end(); i!=e; ++i)
    setParallel(*i);
}

void SCCs::setParallel(Vertices::ID v)
{
  inSequentialStage.reset(v);
}

bool SCCs::mustBeInSequentialStage(const SCC &scc) const
{
  return mustBeInSequentialStage( scc.front() );
}

bool SCCs::mustBeInSequentialStage(Vertices::ID v) const
{
  return inSequentialStage.test(v);
}

void SCCs::print_dot(const PDG &pdg, StringRef dot, StringRef tred, bool bailout) const
{
  {
    std::error_code ec;
    raw_fd_ostream fout(dot, ec, sys::fs::F_None);

    fout << "digraph \"PDG\" {\n";
    if( bailout )
      fout << "\tlabel=\"BAIL OUT OCCURRED before the DAG_SCC completed!\";\n";

    fout << pdg.getV() << *this << pdg.getE() << "}\n";
  }

  runTred(dot.data(),tred.data());
}

void SCCs::print_dot(raw_ostream &fout) const
{
  for(unsigned i=0; i<sccs.size(); ++i)
    print_scc_dot(fout,i);
}

void SCCs::print_scc_dot(raw_ostream &fout, unsigned i) const
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

void SCCs::print_replicated_scc_dot(raw_ostream &fout, unsigned sccno, unsigned stageno, bool first) const
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

void SCCs::getUpperBoundParallelStage(const PDG &pdg, std::vector<Instruction*> &insts) const
{
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
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

bool SCCs::hasNonTrivialParallelStage(const PDG &pdg) const
{
  ControlSpeculation &cspec = pdg.getControlSpeculator();

  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
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
      if( IntrinsicInst *intrin = dyn_cast< IntrinsicInst >(inst) )
      {
        // Intrinsics which do not do anything.
        // (these will return mayReadOrWriteMemory() == true...)
        if( intrin->getIntrinsicID() == Intrinsic::lifetime_start
        ||  intrin->getIntrinsicID() == Intrinsic::lifetime_end
        ||  intrin->getIntrinsicID() == Intrinsic::invariant_start
        ||  intrin->getIntrinsicID() == Intrinsic::invariant_end )
          continue;
      }


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

void SCCs::visit(Vertices::ID vertex, const PartialEdgeSet &G)
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

raw_ostream &operator<<(raw_ostream &fout, const SCCs &sccs)
{
  sccs.print_dot(fout);
  return fout;
}

static void excludeLoopCarriedReflexiveDeps(PDG &pdg, SCCs &sccs)
{
  DEBUG(errs() << "Begin excludeLoopCarriedReflexiveDeps()\n");
  unsigned numNewEdges=0;
  unsigned numQueries=0;

  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &scc = *i;
    if( sccs.mustBeInSequentialStage(scc) )
      continue;

    for(SCCs::SCC::const_iterator j=scc.begin(), z=scc.end(); j!=z; ++j)
    {
      Vertices::ID v = *j;
      ++numQueries;

      if( pdg.queryLoopCarriedMemoryDep(v,v) )
      {
//        DEBUG(errs() << "  Found loop-carried mem dep on " << *pdg.getV().get(v) << '\n');
        ++numNewEdges;
        sccs.setSequential(scc);
        break;
      }
    }
  }

  DEBUG(errs() << "Done excludeLoopCarriedReflexiveDeps(): Queries " << numQueries << ", +" << numNewEdges << '\n');
}

static void excludeLoopCarriedDepsWithinSCC(PDG &pdg, SCCs &sccs)
{
  DEBUG(errs() << "Begin excludeLoopCarriedDepsWithinSCC()\n");
  unsigned numNewEdges=0;
  unsigned numQueries=0;

  const Vertices &V = pdg.getV();

  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &scc = *i;
    if( sccs.mustBeInSequentialStage(scc) )
      continue;

    for(SCCs::SCC::const_iterator j=scc.begin(), z=scc.end(); j!=z; ++j)
    {
      Vertices::ID v = *j;
      Instruction *iv = V.get(v);
      if( ! iv->mayReadOrWriteMemory() )
        continue;

      bool killed = false;
      for(SCCs::SCC::const_iterator k=scc.begin(); k!=z; ++k)
      {
        Vertices::ID w = *k;

        ++numQueries;
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

  DEBUG(errs() << "Done excludeLoopCarriedDepsWithinSCC(): Queries " << numQueries << ",  +" << numNewEdges << '\n');
}

static bool queryLoopCarriedMemoryDep(PDG &pdg, const SCCs::SCC &sources, const SCCs::SCC &dests, unsigned& numQueries)
{
  const Vertices &V = pdg.getV();

  for(SCCs::SCC::const_iterator i=sources.begin(), e=sources.end(); i!=e; ++i)
  {
    Vertices::ID src = *i;
    Instruction *isrc = V.get(src);

    if( !isrc->mayReadOrWriteMemory() )
      continue;

    for(SCCs::SCC::const_iterator j=dests.begin(), z=dests.end(); j!=z; ++j)
    {
      Vertices::ID dst = *j;
      Instruction *idst = V.get(dst);

      if( !idst->mayReadOrWriteMemory() )
        continue;

      ++numQueries;
      if( pdg.queryLoopCarriedMemoryDep(src,dst) )
      {
//        Instruction *idst = V.get(dst);
//        DEBUG(errs() << "Found LC mem dep from " << *isrc  << " to " << *idst << '\n');
        return true;
      }
    }
  }

  return false;
}

static bool queryIntraIterationMemoryDep(PDG &pdg, const SCCs::SCC &sources, const SCCs::SCC &dests, unsigned& numQueries)
{
  const Vertices &V = pdg.getV();

  for(SCCs::SCC::const_iterator i=sources.begin(), e=sources.end(); i!=e; ++i)
  {
    Vertices::ID src = *i;
    Instruction *isrc = V.get(src);

    if( !isrc->mayReadOrWriteMemory() )
      continue;

    for(SCCs::SCC::const_iterator j=dests.begin(), z=dests.end(); j!=z; ++j)
    {
      Vertices::ID dst = *j;
      Instruction *idst = V.get(dst);

      if( !idst->mayReadOrWriteMemory() )
        continue;

      ++numQueries;
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

static bool queryUnrelatedSCCsWithTheGrain(PDG &pdg, SCCs &sccs)
{
  DEBUG(errs() << "Begin queryUnrelatedSCCsWithTheGrain()\n");
  bool mod = false;
  unsigned numNewEdges=0;
  unsigned numQueries=0;

  std::set<unsigned> nonMemSingletons;
  for(unsigned l=0, N=sccs.size(); l<N; ++l) {
    const SCCs::SCC &scc = sccs.get(l);
    if( scc.size() == 1 ) {
      Instruction *inst = pdg.getV().get( scc.front() );
      if( !inst->mayReadOrWriteMemory() )
        nonMemSingletons.insert(l);
    }
  }
  DEBUG(errs() << "nonMemSingletons: " << nonMemSingletons.size() << "/" << sccs.size() << "\n");

  // This list is in REVERSE topological order.
  for(unsigned l=0, N=sccs.size(); l<N; ++l)
  {
    DEBUG(errs() << "- from SCC #" << l << '\n');

    if (nonMemSingletons.count(l))
      continue;

    const SCCs::SCC &later = sccs.get(l);
    /*
    if( later.size() == 1 )
    {
      // A common case: a singleton SCC which contains no memory
      // operations.  Short circuit here to skip a lot of iteration...
      Instruction *inst = pdg.getV().get( later.front() );
      if( !inst->mayReadOrWriteMemory() )
        continue;
    }
    */

    // This list is in REVERSE topological order.
    for(unsigned e=l+1; e<N; ++e)
    {
      if (nonMemSingletons.count(e))
        continue;

      const SCCs::SCC &earlier = sccs.get(e);

      // Either (earlier) is ordered-before (later)
      //     or they are un-ordered.

      // Only do this query if these two SCCs are un-ordered
      if( sccs.hasEdge(pdg, earlier, later) )
        continue;

      if( queryLoopCarriedMemoryDep(pdg, earlier, later, numQueries)
      ||  queryIntraIterationMemoryDep(pdg, earlier, later, numQueries) )
      {
        ++numNewEdges;
        mod = true;
      }
    }
  }

  DEBUG(errs() << "Done queryUnrelatedSCCsWithTheGrain(): Queries " << numQueries << ", +" << numNewEdges <<'\n');
  return mod;
}

static bool queryAgainstTheGrain(PDG &pdg, SCCs &sccs)
{
  DEBUG(errs() << "Begin queryAgainstTheGrain()\n");
  bool mod = false;
  unsigned numNewEdges=0;
  unsigned numQueries=0;

  // This list is in REVERSE topological order.
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &later = *i;

    if( later.size() == 1 )
    {
      // A common case: a singleton SCC which contains no memory
      // operations.  Short circuit here to skip a lot of iteration...
      Instruction *inst = pdg.getV().get( later.front() );
      if( !inst->mayReadOrWriteMemory() )
        continue;
    }

    // This list is in REVERSE topological order.
    for(SCCs::iterator j=i+1; j!=e; ++j)
    {
      assert( i != j );
      const SCCs::SCC &earlier = *j;

      // SCCs lists the strongly connected components
      // in reverse topological order.
      // Thus, either earlier ->* later or
      // they are incomparable.

      // Look for loop-carried deps from any
      // operation in earlier to any operation
      // in later.

      if( queryLoopCarriedMemoryDep(pdg, later, earlier, numQueries)
      ||  queryIntraIterationMemoryDep(pdg, later, earlier, numQueries) )
      {
        ++numNewEdges;
        mod = true;
        break;
      }
    }
  }

  DEBUG(errs() << "Done queryAgainstTheGrain(): queries " << numQueries << ", +" << numNewEdges << '\n');
  return mod;
}


static void queryLoopCarriedMemoryDepsWithinParallelStage(PDG &pdg, SCCs &sccs)
{
  DEBUG(errs() << "Begin queryLoopCarriedMemoryDepsWithinParallelStage()\n");
  unsigned numNewEdges=0;
  unsigned numQueries=0;

  // For each parallel scc
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &later = *i;

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
    for(SCCs::iterator j=i+1; j!=e; ++j)
    {
      assert( i != j );
      const SCCs::SCC &earlier = *j;

      if( sccs.mustBeInSequentialStage(earlier) )
        continue;

      if( queryLoopCarriedMemoryDep(pdg, earlier, later, numQueries) )
      {
        ++numNewEdges;
        break;
      }
    }
  }
  DEBUG(errs() << "Done queryLoopCarriedMemoryDepsWithinParallelStage(): queries " << numQueries << ", +" << numNewEdges << '\n');
}

static void queryLoopCarriedMemoryDepsFromParallelStage(PDG &pdg, SCCs &sccs)
{
  DEBUG(errs() << "Begin queryLoopCarriedMemoryDepsFromParallelStage()\n");
  unsigned numNewEdges=0;
  unsigned numQueries=0;

  // For each parallel scc
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &parallel = *i;

    if( parallel.size() == 1 )
    {
      // A common case: a singleton SCC which contains no memory
      // operations.  Short circuit here to skip a lot of iteration...
      Instruction *inst = pdg.getV().get( parallel.front() );
      if( !inst->mayReadOrWriteMemory() )
        continue;
    }

    if( sccs.mustBeInSequentialStage(parallel) )
      continue;

    // For each non-parallel scc
    for(SCCs::iterator j=sccs.begin(); j!=e; ++j)
    {
      const SCCs::SCC &sequential = *j;

      if( !sccs.mustBeInSequentialStage(sequential) )
        continue;

      if( queryLoopCarriedMemoryDep(pdg, parallel, sequential, numQueries) )
      {
        ++numNewEdges;
        break;
      }
    }
  }
  DEBUG(errs() << "Done queryLoopCarriedMemoryDepsFromParallelStage(): queries " << numQueries << ", +" << numNewEdges << '\n');
}




// Basically, this function performs enough AA queries
// to accuratelly know the structure of the DAG-SCC.
// By carefully choosing which queries to perform, we
// end up performing (on average) about 25% of the
// queries needed to compute the PDG.
//
// You don't need to trust this; instead, see if you
// trust Pipeline::assertPipelineProperty().
bool SCCs::computeDagScc(PDG &pdg, SCCs &sccs, bool abortIfNoParallelStage)
{
  // ------------------- SCC guide further analysis
  sccs.recompute(pdg);

  // How big is the parallel stage?
  if( abortIfNoParallelStage && ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;

  // ------------------- Memory dependences are EXPENSIVE

  // Try to exclude SCCs from the parallel stage
  // via the loop-carried reflex-edge test.
  // This test /cannot/ change the SCCs, but will mark
  // some SCCs as sequential.

  excludeLoopCarriedReflexiveDeps(pdg,sccs);

  // How big is the parallel stage?
  if( abortIfNoParallelStage && ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;

  // Try to exclude SCCs from the parallel stage
  // via the loop-carried within-scc test
  // This test /cannot/ change the SCCs, but will mark
  // some SCCs as sequential.

  excludeLoopCarriedDepsWithinSCC(pdg,sccs);

  // How big is the parallel stage?
  if( abortIfNoParallelStage && ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;

  // sccs lists SCCs in a topological sort of
  // a partial order.  Sometimes, A precedes B
  // yet there is no known dependence from A to B.
  // Remedy that.

//  if( queryUnrelatedSCCsWithTheGrain(pdg,sccs) )
//    sccs.recompute(pdg);
  queryUnrelatedSCCsWithTheGrain(pdg,sccs);

  // Try to merge SCCs via loop-carried cross-scc test.
  // This /can/ change our SCCs, causing them to merge.
  bool cond = queryAgainstTheGrain(pdg,sccs);
  while( cond )
  {
    sccs.recompute(pdg);

    // How big is the parallel stage?
    if( abortIfNoParallelStage && ! sccs.hasNonTrivialParallelStage(pdg) )
      return false;

    queryUnrelatedSCCsWithTheGrain(pdg,sccs);

    cond = queryAgainstTheGrain(pdg,sccs);
  }

  // Query with-the-grain across-sccs
  // consider only parallel SCCs.
  queryLoopCarriedMemoryDepsWithinParallelStage(pdg,sccs);

  // How big is the parallel stage?
  if( abortIfNoParallelStage && ! sccs.hasNonTrivialParallelStage(pdg) )
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

  // How big is the parallel stage?
  if( abortIfNoParallelStage && ! sccs.hasNonTrivialParallelStage(pdg) )
    return false;


  return true;
}

void SCCs::markSequentialSCCs(PDG &pdg, SCCs &sccs)
{
  const Vertices &V = pdg.getV();

  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &scc = *i;
    if( sccs.mustBeInSequentialStage(scc) )
      continue;

    bool killed = false;
    for(SCCs::SCC::const_iterator j=scc.begin(), z=scc.end(); j!=z; ++j)
    {
      Vertices::ID v = *j;
      Instruction *iv = V.get(v);
      if( ! iv->mayReadOrWriteMemory() )
        continue;

      for(SCCs::SCC::const_iterator k=scc.begin(); k!=z; ++k)
      {
        Vertices::ID w = *k;

        if( pdg.hasLoopCarriedEdge(v,w) )
        {
          sccs.setSequential(scc);
          killed = true;
          break;
        }
      }

      if( killed )
        break;
    }
  }
}

}
}

