#define DEBUG_TYPE "pipeline"

#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ReductionDetection.h"
#include "liberty/Analysis/ControlSpecIterators.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/SpecPriv/LoopDominators.h"
#include "liberty/SpecPriv/PDG.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"

#include <sys/time.h>
#include <list>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

cl::opt<bool> AnalysisRegressions(
  "aa-regression", cl::init(false), cl::Hidden,
  cl::desc("Write AA regression information"));
cl::opt<bool> DumpPostDom(
  "specpriv-pdg-dump-post-dom", cl::init(false), cl::Hidden,
  cl::desc("Write Post Dominator Tree and Post Dominance Frontier"));

STATISTIC(numRegDepsCutBecausePredictable, "Num reg deps cut because predictable value");
STATISTIC(numRegDepsCutBecauseCtrlSpec,    "Num reg deps cut because control speculation");

Vertices::Vertices(Loop *l) : map()
{
  loop = l;
  // Represented internally as a monotinically increasing vector of inst pointers.
  // An instruction at map[idx] has id idx.
  // Since pointers are monotonically increasing,
  // we can do binary-search in the forward direction, and constant-time
  // search in the reverse direction.

  // map loop => list of instructions.
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      map.push_back( &*j );
  }

  // Sort into increasing order by pointer address.
  std::sort(map.begin(), map.end());
}

// Forward map: O(log N)
Vertices::ID Vertices::get(const Instruction *inst) const
{
  // Binary search over the vector according
  // to the instruction pointer.
  BiDiMap::const_iterator i =
    std::lower_bound(map.begin(),map.end(), inst);

  assert( i != map.end() && "Instruction not in map (1)");
  assert( inst == *i && "Instruction not in map (2)");

  // Find the index corresponding to this iterator
  return (ID) (i - map.begin());
}

// Reverse map: O(1)
Instruction *Vertices::get(ID idx) const
{
  assert( idx < map.size() && "Idx not in map");
  return map[idx];
}

bool Vertices::count(const Instruction *inst) const
{
  // Binary search over the vector according
  // to the instruction pointer.
  BiDiMap::const_iterator i =
    std::lower_bound(map.begin(),map.end(), inst);

  if( i == map.end() )
    return false;
  else
    return inst == *i;
}

static void escape(raw_ostream &fout, Instruction *inst)
{
  std::string buff;
  raw_string_ostream sout(buff);

  sout << *inst;

  for(unsigned i=0; i < buff.size(); ++i)
  {
    if( buff[i] == '\n' )
      fout << "\\l";
    else if( buff[i] == '\"' )
      fout << "\\\"";
    else
      fout << buff[i];
  }
}

void Vertices::print_dot(raw_ostream &fout, ControlSpeculation *ctrlspec) const
{
  for(unsigned i=0, N=map.size(); i<N; ++i)
  {
    Instruction *inst = map[i];
    fout << "n" << i << "  [shape=box,style=filled,";

    if( ctrlspec && ctrlspec->isSpeculativelyDead(inst) )
      fout << "color=gray,";
    else
      fout << "color=white,";

    fout << "label=\"";

    escape(fout,inst);

    fout << "\"];\n";
  }
}

raw_ostream &operator<<(raw_ostream &fout, const Vertices &v)
{
  v.print_dot(fout);
  return fout;
}

bool PartialEdge::operator==(const PartialEdge &other) const
{
  return this->to_i() == other.to_i();
}

bool PartialEdge::operator<(const PartialEdge &other) const
{
  return this->to_i() < other.to_i();
}

bool PartialEdge::operator&(const PartialEdge &other) const
{
  return (this->to_i() & other.to_i()) != 0;
}

unsigned PartialEdge::to_i() const
{
  // C++ is wonderful.
  assert( sizeof(PartialEdge) == sizeof(unsigned char) );
  unsigned char cc = * reinterpret_cast<const unsigned char*>(this);
  return (unsigned) cc;
}

bool PartialEdge::isEdge() const
{
  return to_i() != 0;
}

void PartialEdge::print(raw_ostream &fout) const
{
  fout << '(';
  if( lc_ctrl )
    fout << "lc_ctrl, ";
  if( ii_ctrl )
    fout << "ii_ctrl, ";

  if( lc_reg )
    fout << "lc_reg, ";
  if( ii_reg )
    fout << "ii_reg, ";

  if( !lc_mem_known )
    fout << "?lc_mem, ";
  else if( lc_mem )
    fout << "lc_mem, ";

  if( !ii_mem_known )
    fout << "?ii_mem, ";
  else if( ii_mem )
    fout << "ii_mem, ";

  fout << ')';
}

raw_ostream &operator<<(raw_ostream &fout, const PartialEdge &pe)
{
  pe.print(fout);
  return fout;
}

// Represents the absence of an edge
static const PartialEdge absence_of_edge = PartialEdge();

PartialEdgeSet::PartialEdgeSet(const Vertices &v)
  : V(v),
    adj_lists( v.size() )
{
  assert( sizeof(WordSizePartialEdgeAggregate) == sizeof(unsigned)
  && "Structure packing assumptions are wrong");

  assert( __is_pod(WordSizePartialEdgeAggregate)
  && "Should be POD for efficient operation");

  assert( ! absence_of_edge.isEdge()
  && "The absence_of_edge-object didn't get zero-initialized");
}

struct AdjacencyLtneKey
{
  bool operator()(const PartialEdgeSet::Adjacency &adj, Vertices::ID key) const
  { return adj.first < key; }
};

PartialEdge &PartialEdgeSet::find(Vertices::ID src, Vertices::ID dst)
{
  AdjacencyList &list = adj_lists[src];

  // Round the destination down to a multiple of WordSizePartialEdgeAggregate::AggregateSize
  Vertices::ID dst_base = dst & ~(WordSizePartialEdgeAggregate::AggregateSize-1);

  AdjacencyLtneKey order;
  AdjacencyList::iterator i = std::lower_bound(list.begin(), list.end(), dst_base, order);

  if( i == list.end() || i->first != dst_base )
  {
    Adjacency entry(dst_base,WordSizePartialEdgeAggregate());
    return list.insert(i, entry)->second.elements[ dst-dst_base ];
  }

  else
    return i->second.elements[ dst-dst_base ];
}

const PartialEdge &PartialEdgeSet::find(Vertices::ID src, Vertices::ID dst) const
{
  const AdjacencyList &adj = adj_lists[src];

  Vertices::ID dst_base = dst & ~(WordSizePartialEdgeAggregate::AggregateSize-1);

  AdjacencyLtneKey order;
  AdjacencyList::const_iterator i = std::lower_bound(adj.begin(), adj.end(), dst_base, order);

  if( i == adj.end() || i->first != dst_base )
    return absence_of_edge;

  else
    return i->second.elements[ dst-dst_base ];
}

unsigned PartialEdgeSet::size() const
{
  unsigned sum=0;
  for(unsigned i=0, N=adj_lists.size(); i<N; ++i)
  {
    const AdjacencyList &list = adj_lists[i];
    for(unsigned j=0, M=list.size(); j<M; ++j)
    {
      const WordSizePartialEdgeAggregate &agg = list[j].second;

      for(unsigned k=0; k< WordSizePartialEdgeAggregate::AggregateSize; ++k)
        if( agg.elements[k].isEdge() )
          ++sum;
    }
  }
  return sum;
}


void PartialEdgeSet::addIICtrl(Vertices::ID src, Vertices::ID dst)
{ find(src,dst).ii_ctrl = true; }
void PartialEdgeSet::addLCCtrl(Vertices::ID src, Vertices::ID dst)
{ find(src,dst).lc_ctrl = true; }
void PartialEdgeSet::addIIReg(Vertices::ID src, Vertices::ID dst)
{ find(src,dst).ii_reg = true; }
void PartialEdgeSet::addLCReg(Vertices::ID src, Vertices::ID dst)
{ find(src,dst).lc_reg = true; }

void PartialEdgeSet::addIIMem(Vertices::ID src, Vertices::ID dst, bool present)
{
  PartialEdge &pe = find(src,dst);
  pe.ii_mem = present;
  pe.ii_mem_known = true;
}
void PartialEdgeSet::addLCMem(Vertices::ID src, Vertices::ID dst, bool present)
{
  PartialEdge &pe = find(src,dst);
  pe.lc_mem = present;
  pe.lc_mem_known = true;
}

void PartialEdgeSet::removeLCCtrl(Vertices::ID src, Vertices::ID dst)
{find(src,dst).lc_ctrl = false;}

void PartialEdgeSet::removeLCReg(Vertices::ID src, Vertices::ID dst)
{find(src,dst).lc_reg = false;}

void PartialEdgeSet::removeLCMem(Vertices::ID src, Vertices::ID dst)
{find(src,dst).lc_mem = false;}

void PartialEdgeSet::removeIICtrl(Vertices::ID src, Vertices::ID dst)
{find(src,dst).ii_ctrl = false;}

void PartialEdgeSet::removeIIReg(Vertices::ID src, Vertices::ID dst)
{find(src,dst).ii_reg = false;}

void PartialEdgeSet::removeIIMem(Vertices::ID src, Vertices::ID dst)
{find(src,dst).ii_mem = false;}

bool PartialEdgeSet::hasLoopCarriedEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.lc_ctrl || pe.lc_reg || pe.lc_mem;
}
bool PartialEdgeSet::knownLoopCarriedEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.lc_mem_known;
}
bool PartialEdgeSet::hasIntraIterationEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.ii_ctrl || pe.ii_reg || pe.ii_mem;
}
bool PartialEdgeSet::knownIntraIterationEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.ii_mem_known;
}
bool PartialEdgeSet::hasEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.ii_ctrl || pe.ii_reg || pe.ii_mem ||
         pe.lc_ctrl || pe.lc_reg || pe.lc_mem;
}
bool PartialEdgeSet::hasEdge(Vertices::ID src, Vertices::ID dst, const PartialEdge &filter) const
{
  const PartialEdge &pe = find(src,dst);
  return (pe & filter) != 0;
}
bool PartialEdgeSet::hasLoopCarriedCtrlEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.lc_ctrl;
}
bool PartialEdgeSet::hasIntraIterationCtrlEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.ii_ctrl;
}
bool PartialEdgeSet::hasLoopCarriedRegEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.lc_reg;
}
bool PartialEdgeSet::hasIntraIterationRegEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.ii_reg;
}
bool PartialEdgeSet::hasLoopCarriedMemEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.lc_mem;
}
bool PartialEdgeSet::hasIntraIterationMemEdge(Vertices::ID src, Vertices::ID dst) const
{
  const PartialEdge &pe = find(src,dst);
  return pe.ii_mem;
}

PartialEdgeSet::iterator PartialEdgeSet::successor_begin(Vertices::ID src) const
{
  PartialEdge filter;
  filter.ii_mem_known =  filter.lc_mem_known = false; // don't care
  filter.ii_reg = filter.ii_ctrl = filter.ii_mem = true;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = true;

  return successor_begin(src,filter);
}
PartialEdgeSet::iterator PartialEdgeSet::successor_begin(Vertices::ID src, const PartialEdge &filter) const
{
  const AdjacencyList &adj = adj_lists[src];
  AdjListIterator iter(adj.begin(), adj.end(), filter);
  iter.skipEmpty();
  return iter;
}


PartialEdgeSet::iterator PartialEdgeSet::successor_end(Vertices::ID src) const
{
  PartialEdge filter;
  filter.ii_mem_known =  filter.lc_mem_known = false; // don't care
  filter.ii_reg = filter.ii_ctrl = filter.ii_mem = true;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = true;

  const AdjacencyList &adj = adj_lists[src];
  return AdjListIterator(adj.end(), adj.end(), filter);
}

void PartialEdgeSet::print(raw_ostream &fout) const
{
  for(unsigned i=0, N=numVertices(); i<N; ++i)
    for(iterator j=successor_begin(i), z=successor_end(i); j!=z; ++j)
    {
      Vertices::ID dst = *j;
      fout << 'n' << i << " -> n" << dst;
      if( hasLoopCarriedEdge(i,dst) )
        fout << " [color=blue]";
      fout << ";\n";
    }
}

raw_ostream &operator<<(raw_ostream &fout, const PartialEdgeSet &e)
{
  e.print(fout);
  return fout;
}

AdjListIterator::AdjListIterator(const AdjListIterator &other)
  : I(other.I), E(other.E), offsetWithinAggregate(other.offsetWithinAggregate), filter(other.filter) {}
AdjListIterator::AdjListIterator(const PartialEdgeSet::AdjacencyList::const_iterator &iter,
                                 const PartialEdgeSet::AdjacencyList::const_iterator &end,
                                 const PartialEdge &F)
  : I(iter), E(end), offsetWithinAggregate(0), filter(F) {}

AdjListIterator &AdjListIterator::operator++()
{
  assert( I != E && "Advance beyond end of adjacency list");
  ++offsetWithinAggregate;
  if( offsetWithinAggregate == WordSizePartialEdgeAggregate::AggregateSize )
  {
    offsetWithinAggregate = 0;
    ++I;
  }
  skipEmpty();

  return *this;
}
Vertices::ID AdjListIterator::operator*() const
{
  assert( I != E && "Dereference beyond end of adjacency list");
  return I->first + offsetWithinAggregate;
}
bool AdjListIterator::operator!=(const AdjListIterator &other) const
{
  return offsetWithinAggregate != other.offsetWithinAggregate
  ||     I != other.I;
}

void AdjListIterator::skipEmpty()
{
  while( I != E )
  {
    const WordSizePartialEdgeAggregate &agg = I->second;
    const PartialEdge &pe = agg.elements[ offsetWithinAggregate ];
    if( filter & pe )
      break;

    ++offsetWithinAggregate;
    if( offsetWithinAggregate == WordSizePartialEdgeAggregate::AggregateSize )
    {
      offsetWithinAggregate = 0;
      ++I;
    }
  }
}

PDG::PDG(const Vertices &v, LoopAA *AA, ControlSpeculation &cs, PredictionSpeculation &predspec, const DataLayout *td, bool ignore, bool constrainSubLoopsIntoWholeSCCs)
: numQueries(0), numComplaints(0), numUsefulRemedies(0), numUnneededRemedies(0), V(v), E(V), ctrlspec(cs), aa(AA), ignoreAntiOutput(ignore)
{
  if( AnalysisRegressions )
  {
    Loop *loop = v.getLoop();
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();

    errs() << "#Analysis-regressions: Begin loop '" << fcn->getName() << "' :: '" << header->getName() << "'\n";
  }

  errs() << "\t*** computeRegisterDeps...\n";
  errs().flush();

  computeRegisterDeps(ctrlspec, predspec);

  errs() << "\t*** computeControlDeps...\n";
  errs().flush();

  computeControlDeps(ctrlspec, td);

  errs() << "\t*** constrainSubLoops (" << constrainSubLoopsIntoWholeSCCs << ")...\n";
  errs().flush();

  if( constrainSubLoopsIntoWholeSCCs )
    constrainSubLoops();

  errs() << "\t*** computeMemoryDeps...\n";
  errs().flush();

  computeExhaustivelyMemDeps();
}


PDG::PDG(const Vertices &v, ControlSpeculation &cs, PredictionSpeculation &predspec, const DataLayout *td, bool ignore, bool constrainSubLoopsIntoWholeSCCs)
: numQueries(0), numComplaints(0), numUsefulRemedies(0), numUnneededRemedies(0), V(v), E(V), ctrlspec(cs), ignoreAntiOutput(ignore)
{
  if( AnalysisRegressions )
  {
    Loop *loop = v.getLoop();
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();

    errs() << "#Analysis-regressions: Begin loop '" << fcn->getName() << "' :: '" << header->getName() << "'\n";
  }

  errs() << "\t*** computeRegisterDeps...\n";
  errs().flush();

  computeRegisterDeps(ctrlspec, predspec);

  errs() << "\t*** computeControlDeps...\n";
  errs().flush();

  computeControlDeps(ctrlspec, td);

  errs() << "\t*** constrainSubLoops (" << constrainSubLoopsIntoWholeSCCs << ")...\n";
  errs().flush();

  if( constrainSubLoopsIntoWholeSCCs )
    constrainSubLoops();
}

PDG::~PDG()
{
  DEBUG(pstats( errs() ));
  if( AnalysisRegressions )
    errs() << "#Analysis-regressions: End loop\n";
}

static unsigned Npdg=0, Nadj=0;
static std::map<unsigned,unsigned> histo;

void PartialEdgeSet::pstats(raw_ostream &fout) const
{
  fout << "sizeof(PartialEdge) = " << sizeof(PartialEdge) << '\n';

  for(Vertices::ID i=0; i<V.size(); ++i)
    histo[ adj_lists[i].size() ]++;
  ++Npdg;
  Nadj += V.size();

  fout << "Histogram of adjacency list lengths (averaged over " << Npdg << " pdgs; " << Nadj << " adj lists)\n";
  const float step=0.05;
  float sum=0.;
  float limit=step;
  for(std::map<unsigned,unsigned>::iterator i=histo.begin(), e=histo.end(); i!=e; ++i)
  {
    const float frac = i->second/(float)Nadj;
    const float oldSum = sum;
    sum += frac;
//    fout << " len " << i->first << " occurred " << 100*frac << "% of the time; cdf " << (unsigned)(100*sum) << '\n';

    if( oldSum < limit && limit <= sum )
    {
      fout << " o " << (unsigned)(100*sum) << "% adj-lists are length " << i->first << " or shorter\n";

      while( limit <= sum )
        limit += step;
    }
  }
  fout << '\n';
}

void PDG::pstats(raw_ostream &fout) const
{
  fout << "PDG issued " << numQueries << " memory queries.\n";

  unsigned numMemOps = 0;
  for(unsigned i=0; i<V.size(); ++i)
  {
    Instruction *inst = V.get(i);
    if( inst->mayReadOrWriteMemory() )
      ++numMemOps;
  }

  const unsigned eachToEach = numMemOps * numMemOps;
  const unsigned queriesPerDep = 2;
  const unsigned nII = queriesPerDep * eachToEach;
  const unsigned nLC = queriesPerDep * eachToEach;
  const unsigned worstCase = nII + nLC;

  fout << "  This is " << (100 * numQueries / worstCase) << "% of " << worstCase << " worst case.\n";

  getE().pstats(fout);
}

void PDG::computeRegisterDeps(ControlSpeculation &ctrlspec, PredictionSpeculation &predspec)
{
  Loop *loop = V.getLoop();
  //ReductionDetection reduxdet;

  // Add register dependences.

  for(Vertices::ID u=0, N=V.size(); u<N; ++u)
  {
    const Instruction *user = V.get(u);
    const PHINode *phi = dyn_cast<PHINode>( user );
    const bool loopCarried = (user->getParent() == loop->getHeader() && phi);
    const bool predictable = predspec.isPredictable(user,loop);

    // For each operand of user which is also an instruction in the loop
    for(User::const_op_iterator j=user->op_begin(), z=user->op_end(); j!=z; ++j)
    {
      const Value *operand = *j;

      if( const Instruction *src = dyn_cast<Instruction>(operand) )
        if( V.count(src) )
        {
          // Perhaps control speculation makes this register
          // dependence impossible.
          if( phi && ctrlspec.phiUseIsSpeculativelyDead(phi, src) )
          {
            ++numRegDepsCutBecauseCtrlSpec;
            continue;
          }

          // Perhaps this register dependence is speculatively
          // predictable.
          if( loopCarried && predictable )
          {
            ++numRegDepsCutBecausePredictable;
            continue;
          }

          // No, this is a /hard/ register dependence.
          Vertices::ID s = V.get(src);

          if( loopCarried ) {
            //errs() << "new LC reg dep between " << *src << " and " << *user << "\n";
            E.addLCReg(s,u);
           }

          else
            E.addIIReg(s,u);
        }
    }
  }
}

void PDG::buildTransitiveIntraIterationControlDependenceCache(PartialEdgeSet& cache) const
{
  Loop *loop = V.getLoop();
  std::list<Vertices::ID> fringe;

  // initialization phase with the PartialEdgeSet of the pdg

  PartialEdge filter;
  filter.ii_reg = filter.ii_mem = filter.ii_mem_known = false;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = filter.lc_mem_known = false;
  filter.ii_ctrl = true;

  for(Loop::block_iterator j=loop->block_begin(), z=loop->block_end(); j!=z; ++j)
    for(BasicBlock::iterator k=(*j)->begin(), g=(*j)->end(); k!=g; ++k)
    {
      Instruction *inst = &*k;
      Vertices::ID s = V.get( inst );
      fringe.push_back(s);

      PartialEdgeSet::iterator si = E.successor_begin(s, filter), se = E.successor_end(s);
      for ( ; si != se ; ++si)
        cache.addIICtrl(s, *si);
    }

  // update cache iteratively

  while (!fringe.empty()) {
    Vertices::ID v = fringe.front();
    fringe.pop_front();

    std::vector<Vertices::ID> updates;

    PartialEdgeSet::iterator j=cache.successor_begin(v), z=cache.successor_end(v);
    for ( ; j!=z ; ++j) {
      Vertices::ID m = *j;
      PartialEdgeSet::iterator k=cache.successor_begin(m), g=cache.successor_end(m);
      for ( ; k!=g ; ++k) {
        if (!cache.hasIntraIterationEdge(v, *k)) // all II edge is a II-ctrl edge
          updates.push_back(*k);
      }
    }

    if (!updates.empty()) {
      for (unsigned i = 0 ; i < updates.size() ; i++)
        cache.addIICtrl(v, updates[i]);
      fringe.push_back(v);
    }
  }
}

bool PDG::hasTransitiveIntraIterationControlDependence(Vertices::ID src, Vertices::ID dst) const
{
  Loop *loop = V.getLoop();

  std::list< Vertices::ID > fringe;
  std::set< Vertices::ID >  visited;
  fringe.push_back( dst );

  while( !fringe.empty() )
  {
    Vertices::ID v = fringe.front();
    fringe.pop_front();

    if ( v == src )
      return true;

    // assert( !visited.count(v) && "control dependence tree has a cycle" );
    if ( visited.count(v) )
      continue;
    visited.insert(v);

    for(Loop::block_iterator j=loop->block_begin(), z=loop->block_end(); j!=z; ++j)
      for(BasicBlock::iterator k=(*j)->begin(), g=(*j)->end(); k!=g; ++k)
      {
        Instruction *inst = &*k;
        Vertices::ID s = V.get( inst );

        const PartialEdge &edge = ( const_cast<const PartialEdgeSet&>( E ) ).find(s, v);
        if ( edge.isEdge() && edge.ii_ctrl )
        {
          fringe.push_back(s);
          // errs() << "\t" << s << "->" << v << "\n";
        }
      }
  }

  return false;
}

void PDG::computeControlDeps(ControlSpeculation &ctrlspec, const DataLayout *td)
{
  Loop *loop = V.getLoop();

  // Add intra-iteration control dependences.

  LoopPostDom pdt(ctrlspec, loop);
  if( DumpPostDom )
  {
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    {
      Twine filename = Twine("postdoms.") + fcn->getName() + "-" + header->getName() + ".txt";

      errs() << "Writing " << filename << "...";

      //std::string errinfo;
      std::error_code ec;
      raw_fd_ostream fout(filename.str().c_str(), ec, sys::fs::F_None);

      fout << "Post dominatance:\n\n";
      pdt.printPD(fout);

      fout << "\n\nImmediate post-dominance relation:\n\n";
      pdt.printIPD(fout);

      fout << "\n\nPost dominance frontier:\n\n";
      pdt.printPDF(fout);

      errs() << " done.\n";
    }

    {
      Twine filename = Twine("ipd.") + fcn->getName() + "-" + header->getName() + ".dot";

      errs() << "Writing " << filename << "...";

      //std::string errinfo;
      std::error_code ec;
      raw_fd_ostream fout(filename.str().c_str(), ec, sys::fs::F_None);

      pdt.printIPD_dot(fout);
    }
  }

  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock dst = ControlSpeculation::LoopBlock( *i );
    for(LoopPostDom::pdf_iterator j=pdt.pdf_begin(dst), z=pdt.pdf_end(dst); j!=z; ++j)
    {
      ControlSpeculation::LoopBlock src = *j;

      TerminatorInst *term = src.getBlock()->getTerminator();
      assert( !ctrlspec.isSpeculativelyUnconditional(term)
      && "Unconditional branches do not source control deps (ii)");

      Vertices::ID t = V.get(term);
      for(BasicBlock::iterator k=dst.getBlock()->begin(), f=dst.getBlock()->end(); k!=f; ++k)
      {
        Instruction *idst = &*k;

        /*
        // Draw ctrl deps to:
        //  (1) Operations with side-effects
        //  (2) Conditional branches.
        if( isSafeToSpeculativelyExecute(idst,td) )
          continue;
        */

        Vertices::ID s = V.get( idst );
        E.addIICtrl(t, s);
      }
    }
  }

  // TODO: ideally, a PHI dependence is drawn from
  // a conditional branch to a PHI node iff the branch
  // controls which incoming value is selected by that PHI.

  // That's a pain to compute.  Instead, we will draw a
  // dependence from branches to PHIs in successors.
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();
    Vertices::ID t = V.get(term);

    // no control dependence can be formulated around unconditional branches

    if (ctrlspec.isSpeculativelyUnconditional(term))
      continue;

    for(ControlSpeculation::succ_iterator j=ctrlspec.succ_begin(bb), z=ctrlspec.succ_end(bb); j!=z; ++j)
    {
      BasicBlock *succ = *j;
      if( !loop->contains(succ) )
        continue;

      const bool loop_carried = (succ == loop->getHeader());

      for(BasicBlock::iterator k=succ->begin(); k!=succ->end(); ++k)
      {
        PHINode *phi = dyn_cast<PHINode>(&*k);
        if( !phi )
          break;
        if( phi->getNumIncomingValues() == 1 )
          continue;
        Vertices::ID p = V.get(phi);

        if( loop_carried )
        {
          //errs() << "new LC ctrl dep between " << *term << " and " << *phi << "\n";
          E.addLCCtrl(t,p);
        }
        else
          E.addIICtrl(t,p);
      }
    }
  }

  // Add loop-carried control dependences.
  // Foreach loop-exit.
  typedef ControlSpeculation::ExitingBlocks Exitings;

  // build a partialEdgeSet that holds transitive II-ctrl dependence info
  PartialEdgeSet IICtrlCache(V);
  buildTransitiveIntraIterationControlDependenceCache(IICtrlCache);

  Exitings exitings;
  ctrlspec.getExitingBlocks(loop, exitings);
  for(Exitings::iterator i=exitings.begin(), e=exitings.end(); i!=e; ++i)
  {
    BasicBlock *exiting = *i;
    TerminatorInst *term = exiting->getTerminator();
    assert( !ctrlspec.isSpeculativelyUnconditional(term)
    && "Unconditional branches do not source control deps (lc)");

    Vertices::ID t = V.get(term);

    // Draw ctrl deps to:
    //  (1) Operations with side-effects
    //  (2) Loop exits.
    for(Loop::block_iterator j=loop->block_begin(), z=loop->block_end(); j!=z; ++j)
    {
      BasicBlock *dst = *j;
      for(BasicBlock::iterator k=dst->begin(), g=dst->end(); k!=g; ++k)
      {
        Instruction *idst = &*k;

        /*
        // Draw ctrl deps to:
        //  (1) Operations with side-effects
        //  (2) Loop exits
        if( isSafeToSpeculativelyExecute(idst,td) )
          continue;
        */

        /*
        if( TerminatorInst *tt = dyn_cast< TerminatorInst >(idst) )
          if( ! ctrlspec.mayExit(tt,loop) )
            continue;
        */

        Vertices::ID s = V.get( idst );

        // Draw LC ctrl dep only when there is no (transitive) II ctrl dep from t to s

        //if ( hasTransitiveIntraIterationControlDependence(t, s) )
        if ( IICtrlCache.hasEdge(t, s) )
          continue;
        //errs() << "new LC ctrl dep between " << *term << " and " << *idst << "\n";
        E.addLCCtrl(t, s);
      }
    }
  }
}

void PDG::computeExhaustivelyMemDeps()
{
  const unsigned N=V.size();

  for(unsigned i=0; i<N; ++i)
  {
    Instruction *ii = V.get(i);
    if( ! ii->mayReadOrWriteMemory() )
      continue;

    for(unsigned j=0; j<N; ++j)
    {
      Instruction *jj = V.get(j);
      if( ! jj->mayReadOrWriteMemory() )
        continue;

      queryLoopCarriedMemoryDep(i,j,true);
      queryIntraIterationMemoryDep(i,j,true);
    }
  }
}


bool PDG::queryMemoryDep(Vertices::ID src, Vertices::ID dst, LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV)
{
  Instruction *sop = V.get(src), *dop = V.get(dst);

  return queryMemoryDep(sop,dop,FW,RV);
}


LoopAA::ModRefResult PDG::query(Instruction *sop, LoopAA::TemporalRelation rel, Instruction *dop, Loop *loop)
{
//  struct timeval start, stop;

  ++numQueries;

//  gettimeofday(&start,0);
  const LoopAA::ModRefResult res = aa->modref(sop,rel,dop,loop);
//  gettimeofday(&stop,0);

//  const uint64_t microseconds = 1e6*(stop.tv_sec - start.tv_sec) + (stop.tv_usec - start.tv_usec);
//  totalAATime += microseconds;

  /*
  errs() << "new mem query between i1: " << *sop << " and i2: " << *dop
         << "for loop with loop_header " << loop->getHeader()->getName().str()
         << "\n THis query returned " << res << "   temporal rel: " << rel << "\n";

  if (res == LoopAA::NoModRef)
    errs() << "This query return nomodref\n";
  */

  return res;
}

static void regressionsPrintInst(const Instruction *src)
{
  const BasicBlock *srcbb  = src->getParent();
  const Function   *srcfcn = srcbb->getParent();
  unsigned offsetWithinBlock = 0;
  for(BasicBlock::const_iterator i=srcbb->begin(), e=srcbb->end(); i!=e; ++i, ++offsetWithinBlock)
    if( src == &*i )
      break;

  errs() << "('";
  errs() << srcfcn->getName() << "' :: '" << srcbb->getName() << "' :: ";
  if( src->hasName() )
    errs() << "'" << src->getName() << "' ";
  else
    errs() << "'' ";

  errs() << "(" << offsetWithinBlock << ") ";
  errs() << ") ";
}

static void regressionsReport(const Instruction *src, const Instruction *dst,
  const LoopAA::TemporalRelation fwtime, const LoopAA::ModRefResult fwres,
  const LoopAA::TemporalRelation rvtime, const LoopAA::ModRefResult rvres)
{
  if( !AnalysisRegressions )
    return;

  errs() << "#Analysis-regressions: ModRef ";

  errs() << "Source ";
  regressionsPrintInst(src);

  errs() << "Destination ";
  regressionsPrintInst(dst);

  if( fwtime == LoopAA::Same && rvtime == LoopAA::Same )
    errs() << "Intra-iteration ";
  else if( fwtime == LoopAA::Before && rvtime == LoopAA::After )
    errs() << "Loop-carried ";
  else
    errs() << "Bad-time ";

  errs() << "Result ";

  const bool srcmod = (fwres == LoopAA::Mod || fwres == LoopAA::ModRef);
  const bool srcref = (fwres == LoopAA::Ref || fwres == LoopAA::ModRef);

  const bool dstmod = (rvres == LoopAA::Mod || rvres == LoopAA::ModRef);
  const bool dstref = (rvres == LoopAA::Ref || rvres == LoopAA::ModRef);

  // Flow: write to read
  if( srcmod && dstref )
    errs() << "Flow ";
  else
    errs() << "No-flow ";

  // Anti: read to write
  if( srcref && dstmod )
    errs() << "Anti ";
  else
    errs() << "No-anti ";

  // Output: write to write
  if( srcmod && dstmod )
    errs() << "Output ";
  else
    errs() << "No-output ";

  errs() << '\n';
}

bool PDG::queryMemoryDep(Instruction *sop, Instruction *dop, LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV)
{
  if( ! sop->mayReadOrWriteMemory() )
    return false;
  if( ! dop->mayReadOrWriteMemory() )
    return false;
  if( ! sop->mayWriteToMemory() && ! dop->mayWriteToMemory() )
    return false;

  Loop *loop = V.getLoop();

  // forward dep test
  const LoopAA::ModRefResult forward = query(sop, FW, dop, loop);
  if( LoopAA::NoModRef == forward )
  {
    regressionsReport( sop, dop, FW, forward, RV, LoopAA::NoModRef );
    return false;
  }

  // Mod, ModRef, or Ref

  LoopAA::ModRefResult reverse = forward;
  if( FW != RV || sop != dop )
  {
    // reverse dep test
    reverse = query(dop, RV, sop, loop);

    if( LoopAA::NoModRef == reverse )
    {
      regressionsReport( sop, dop, FW, forward, RV, reverse );
      return false;
    }
  }

  if( LoopAA::Ref == forward && LoopAA::Ref == reverse )
  {
    regressionsReport( sop, dop, FW, forward, RV, reverse );
    return false; // RaR dep; who cares.
  }

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.
  regressionsReport( sop, dop, FW, forward, RV, reverse );

  // Which result does the caller want?
  if( ignoreAntiOutput && FW != RV )
  {
    if( forward == LoopAA::Mod || forward == LoopAA::ModRef )   // from Write
      if( reverse == LoopAA::Ref || reverse == LoopAA::ModRef ) // to Read
        return true;
    return false;
  }


  // ignore intra-iteration anti dependence
  // if there is a flow dependence on reverse case.
  if( ignoreAntiOutput && FW == RV )
  {
    if( forward == LoopAA::Ref )   // from Read
      if( reverse == LoopAA::Mod || reverse == LoopAA::ModRef ) // to Write
        return false;
  }


  return true;
}

bool PDG::queryIntraIterationMemoryDep(Vertices::ID src, Vertices::ID dst, bool force)
{
  const bool hasEdge = E.hasIntraIterationEdge(src,dst);
  if( (!force && hasEdge) || E.knownIntraIterationEdge(src,dst) )
    return hasEdge;

  Loop *loop = V.getLoop();

  bool maybeDep = false;

  Instruction *sop = V.get(src), *dop = V.get(dst);

  const clock_t begin_time = clock();

  if( ctrlspec.isReachable(sop,dop,loop) )
    maybeDep = queryMemoryDep(sop,dop, LoopAA::Same,LoopAA::Same);

  float diff = float( clock () - begin_time ) /  CLOCKS_PER_SEC;

  if ( diff > 10.0 )
    errs() << "\t--- a query took more than 10 secs\n";
  if ( diff > 60.0 )
    errs() << "\t\t--- a query took more than a min!\n";
  E.addIIMem(src,dst, maybeDep);

  return maybeDep;
}

bool PDG::queryLoopCarriedMemoryDep(Vertices::ID src, Vertices::ID dst, bool force)
{
  const bool hasEdge = E.hasLoopCarriedEdge(src,dst);
  if( (!force && hasEdge) || E.knownLoopCarriedEdge(src,dst) )
    return hasEdge;

  const clock_t begin_time = clock();

  bool maybeDep = queryMemoryDep(src,dst, LoopAA::Before,LoopAA::After);

  if ( float( clock () - begin_time ) /  CLOCKS_PER_SEC > 1.0 )
    errs() << "--- query took more than a sec\n";

  E.addLCMem(src,dst, maybeDep);

  return maybeDep;
}

bool PDG::hasEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasEdge(src,dst);
}

bool PDG::hasLoopCarriedEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasLoopCarriedEdge(src,dst);
}

bool PDG::unknown(Vertices::ID src, Vertices::ID dst) const
{
  return !E.knownLoopCarriedEdge(src,dst)
    &&   !E.knownIntraIterationEdge(src,dst);
}

bool PDG::unknownLoopCarried(Vertices::ID src, Vertices::ID dst) const
{
  return !E.knownLoopCarriedEdge(src,dst);
}

bool PDG::hasLoopCarriedCtrlEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasLoopCarriedCtrlEdge(src,dst);
}

bool PDG::hasIntraIterationCtrlEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasIntraIterationCtrlEdge(src,dst);
}

bool PDG::hasLoopCarriedRegEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasLoopCarriedRegEdge(src,dst);
}

bool PDG::hasIntraIterationRegEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasIntraIterationRegEdge(src,dst);
}

bool PDG::hasLoopCarriedMemEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasLoopCarriedMemEdge(src,dst);
}

bool PDG::hasIntraIterationMemEdge(Vertices::ID src, Vertices::ID dst) const
{
  return E.hasIntraIterationMemEdge(src,dst);
}

void PDG::constrainSubLoops()
{
  Loop *loop = V.getLoop();

  for(Loop::iterator i=loop->begin(), e=loop->end(); i!=e; ++i)
    constrainSubLoop(*i);
}

int PDG::removableLoopCarriedEdge(Vertices::ID src, Vertices::ID dst) const {
  int cost = 0;
  if (hasLoopCarriedMemEdge(src,dst)) {
    const auto it = remediatedLCMemEdgesCostMap.find(std::make_pair(src,dst));
    if (it != remediatedLCMemEdgesCostMap.end())
      cost += it->second;
    else
      return -1;
  }

  if (hasLoopCarriedCtrlEdge(src,dst)) {
    const auto it = remediatedLCCtrlEdgesCostMap.find(std::make_pair(src,dst));
    if (it != remediatedLCCtrlEdgesCostMap.end())
      cost += it->second;
    else
      return -1;
  }

  if (hasLoopCarriedRegEdge(src,dst)) {
    const auto it = remediatedLCRegEdgesCostMap.find(std::make_pair(src,dst));
    if (it != remediatedLCRegEdgesCostMap.end())
      cost += it->second;
    else
      return -1;
  }

  return cost;
}

void PDG::removeEdge(Vertices::ID src, Vertices::ID dst, bool lc, DepType dt) {
  if (lc) {
    if (dt == DepType::Mem)
      removeLoopCarriedMemEdge(src,dst);
    else if (dt == DepType::Reg)
      removeLoopCarriedRegEdge(src,dst);
    else
      removeLoopCarriedCtrlEdge(src,dst);
  } else {
    if (dt == DepType::Mem)
      removeIntraIterationMemEdge(src,dst);
    else if (dt == DepType::Reg)
      removeIntraIterationRegEdge(src,dst);
    else
      removeIntraIterationCtrlEdge(src,dst);
  }
}

void PDG::removeLoopCarriedMemEdge(Vertices::ID src, Vertices::ID dst) {
  E.removeLCMem(src, dst);
}

void PDG::removeLoopCarriedCtrlEdge(Vertices::ID src, Vertices::ID dst) {
  E.removeLCCtrl(src, dst);
}

void PDG::removeLoopCarriedRegEdge(Vertices::ID src, Vertices::ID dst) {
  E.removeLCReg(src, dst);
}

void PDG::removeIntraIterationMemEdge(Vertices::ID src, Vertices::ID dst) {
  E.removeIIMem(src, dst);
}

void PDG::removeIntraIterationCtrlEdge(Vertices::ID src, Vertices::ID dst) {
  E.removeIICtrl(src, dst);
}

void PDG::removeIntraIterationRegEdge(Vertices::ID src, Vertices::ID dst) {
  E.removeIIReg(src, dst);
}

void PDG::setRemediatedEdgeCost(int cost, Vertices::ID src, Vertices::ID dst,
                                bool lc, DepType dt) {
  if (lc) {
    if (dt == DepType::Mem)
      remediatedLCMemEdgesCostMap[std::make_pair(src, dst)] = cost;
    else if (dt == DepType::Reg)
      remediatedLCRegEdgesCostMap[std::make_pair(src, dst)] = cost;
    else
      remediatedLCCtrlEdgesCostMap[std::make_pair(src, dst)] = cost;
  } else {
    if (dt == DepType::Mem)
      remediatedIIMemEdgesCostMap[std::make_pair(src, dst)] = cost;
    else if (dt == DepType::Reg)
      remediatedIIRegEdgesCostMap[std::make_pair(src, dst)] = cost;
    else
      remediatedIICtrlEdgesCostMap[std::make_pair(src, dst)] = cost;
  }
}

void PDG::constrainSubLoop(Loop *loop)
{
  Vertices::ID first=~0U, prev=~0U;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      Vertices::ID vi = V.get(inst);

      if( ~0U == first )
        first = vi;

      if( ~0U != prev )
        E.addIIReg(prev,vi);

      prev = vi;
    }
  }
  E.addIIReg(prev,first);
}

bool PDG::isDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.ii_mem_known =  filter.lc_mem_known = false; // don't care
  filter.ii_reg = filter.ii_ctrl = filter.ii_mem = true;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = true;
  return isDependent(src, dst, filter);
}

bool PDG::isDependent(
    Vertices::ID src, Vertices::ID dst, const PartialEdge &filter) const
{
  std::set< Vertices::ID > visited;
  return isDependent(src, dst, filter, visited);
}

bool PDG::isDependent(
    Vertices::ID src, Vertices::ID dst, const PartialEdge &filter,
    std::set< Vertices::ID > &visited) const
{
  if (visited.count(src))
    return false;
  if (E.hasEdge(src, dst, filter))
    return true;
  visited.insert(src);

  PartialEdgeSet::iterator i=E.successor_begin(src, filter),
                           j=E.successor_end(src);
  for ( ; i!=j ; ++i) {
    Vertices::ID v = *i;
    if (isDependent(v, dst, filter, visited))
      return true;
  }
  return false;
}

bool PDG::isIntraIterationDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.ii_mem_known =  filter.lc_mem_known = false; // don't care
  filter.ii_reg = filter.ii_ctrl = filter.ii_mem = true;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = false;
  return isDependent(src, dst, filter);
}

bool PDG::isLoopCarriedDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.ii_mem_known =  filter.lc_mem_known = false; // don't care
  filter.ii_reg = filter.ii_ctrl = filter.ii_mem = false;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = true;
  return isDependent(src, dst, filter);
}

bool PDG::isIntraIterationRegDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.ii_mem_known =  filter.lc_mem_known = false; // don't care
  filter.ii_reg = true;
  filter.ii_ctrl = filter.ii_mem = false;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = false;
  return isDependent(src, dst, filter);
}

bool PDG::isIntraIterationCtrlDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.ii_mem_known =  filter.lc_mem_known = false; // don't care
  filter.ii_ctrl = true;
  filter.ii_reg = filter.ii_mem = false;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = false;
  return isDependent(src, dst, filter);
}

bool PDG::isIntraIterationMemDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.ii_mem_known =  filter.lc_mem_known = false; // don't care
  filter.ii_mem = true;
  filter.ii_reg = filter.ii_ctrl = false;
  filter.lc_reg = filter.lc_ctrl = filter.lc_mem = false;
  return isDependent(src, dst, filter);
}

bool PDG::isLoopCarriedRegDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.lc_mem_known =  filter.ii_mem_known = false; // don't care
  filter.lc_reg = true;
  filter.lc_ctrl = filter.lc_mem = false;
  filter.ii_reg = filter.ii_ctrl = filter.ii_mem = false;
  return isDependent(src, dst, filter);
}

bool PDG::isLoopCarriedCtrlDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.lc_mem_known =  filter.ii_mem_known = false; // don't care
  filter.lc_ctrl = true;
  filter.lc_reg = filter.lc_mem = false;
  filter.ii_reg = filter.ii_ctrl = filter.ii_mem = false;
  return isDependent(src, dst, filter);
}

bool PDG::isLoopCarriedMemDependent(Vertices::ID src, Vertices::ID dst) const
{
  PartialEdge filter;
  filter.lc_mem_known =  filter.ii_mem_known = false; // don't care
  filter.lc_mem = true;
  filter.lc_reg = filter.lc_ctrl = false;
  filter.ii_reg = filter.ii_ctrl = filter.ii_mem = false;
  return isDependent(src, dst, filter);
}

PDG::PDG(const Vertices &vertices, PartialEdgeSet &edges, ControlSpeculation &cs, bool ignoreAO, LoopAA *stack)
  : numComplaints(0), numUsefulRemedies(0), numUnneededRemedies(0), V(vertices), E(edges), ctrlspec(cs), ignoreAntiOutput(ignoreAO), aa(stack) {}

PDG::PDG(PDG &pdg, const Vertices &v, ControlSpeculation &cs, bool ignoreAO)
    : V(v), E(V), ctrlspec(cs), ignoreAntiOutput(ignoreAO) {

  aa = pdg.getAA();
  //remed = pdg.getRemed();

  // copy vertices and edges
  // vertices are not expected to change in most cases but copy to make sure
  // they remain unchanged for different strategies
  /*
  const Vertices &origV = pdg.getV();
  Loop* loop = origV.getLoop();
  V(loop);
  */

  const PartialEdgeSet &origE = pdg.getE();
  for (unsigned i = 0, N = origE.numVertices(); i < N; ++i) {
    for (PartialEdgeSet::iterator j = origE.successor_begin(i),
                                  z = origE.successor_end(i);
         j != z; ++j) {
      Vertices::ID dst = *j;
      if (origE.hasLoopCarriedCtrlEdge(i, dst))
        E.addLCCtrl(i, dst);
      if (origE.hasIntraIterationCtrlEdge(i, dst))
        E.addIICtrl(i, dst);
      if (origE.hasLoopCarriedRegEdge(i, dst))
        E.addLCReg(i, dst);
      if (origE.hasIntraIterationRegEdge(i, dst))
        E.addIIReg(i, dst);
      if (origE.hasLoopCarriedMemEdge(i, dst))
        E.addLCMem(i, dst);
      if (origE.hasIntraIterationMemEdge(i, dst))
        E.addIIMem(i, dst);
    }
  }
}

}
}

