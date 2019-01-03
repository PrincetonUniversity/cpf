#define DEBUG_TYPE "spec-priv-smtx-manager"

#include "SmtxManager.h"
#include "llvm/ADT/Statistic.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numAssumptionsLC, "Number of loop-carried no-flow assumptions");
STATISTIC(numAssumptionsII, "Number of intra-iteration no-flow assumptions");

STATISTIC(numUnspeculatedLC, "Number of UNspeculated loop-carried no-flow assumptions");
STATISTIC(numUnspeculatedII, "Number of UNspeculated intra-iteration no-flow assumptions");

void SmtxSpeculationManager::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< LAMPLoadProfile >();
  au.setPreservesAll();
}

bool SmtxSpeculationManager::runOnModule(Module &mod)
{
  lampResult = & getAnalysis< LAMPLoadProfile >();
  return false;
}

static const SmtxSpeculationManager::Assumptions empty_list;

// Order by the 'dst' element, ascending, and then by the 'src' element.
bool SmtxSpeculationManager::Assumption::operator<(const Assumption &other) const
{
  if( dst < other.dst )
    return true;
  else if( dst > other.dst )
    return false;
  else
    return src < other.src;
}

bool SmtxSpeculationManager::Assumption::operator==(const Assumption &other) const
{
  return src == other.src && dst == other.dst;
}

bool SmtxSpeculationManager::Assumption::operator!=(const Assumption &other) const
{
  return !((*this) == other);
}

const SmtxSpeculationManager::Assumptions &SmtxSpeculationManager::getLC(const Loop *loop) const
{
  Loop2Assumptions::const_iterator i = lcNoFlow.find( loop->getHeader() );
  if( i == lcNoFlow.end() )
    return empty_list;
  else
    return i->second;
}
const SmtxSpeculationManager::Assumptions &SmtxSpeculationManager::getII(const Loop *loop) const
{
  Loop2Assumptions::const_iterator i = iiNoFlow.find( loop->getHeader() );
  if( i == iiNoFlow.end() )
    return empty_list;
  else
    return i->second;
}

SmtxSpeculationManager::Assumptions &SmtxSpeculationManager::getLC(const Loop *loop)
{
  return lcNoFlow[ loop->getHeader() ];
}
SmtxSpeculationManager::Assumptions &SmtxSpeculationManager::getII(const Loop *loop)
{
  return iiNoFlow[ loop->getHeader() ];
}

SmtxSpeculationManager::iterator SmtxSpeculationManager::begin_lc(const Loop *loop) const
{
  return getLC(loop).begin();
}
SmtxSpeculationManager::iterator SmtxSpeculationManager::end_lc(const Loop *loop) const
{
  return getLC(loop).end();
}

SmtxSpeculationManager::iterator SmtxSpeculationManager::begin_ii(const Loop *loop) const
{
  return getII(loop).begin();
}
SmtxSpeculationManager::iterator SmtxSpeculationManager::end_ii(const Loop *loop) const
{
  return getII(loop).end();
}

bool SmtxSpeculationManager::isAssumedLC(const Loop *loop, const Instruction *src, const Instruction *dst) const
{
  Assumption assumption(src,dst);
  iterator B = begin_lc(loop), E = end_lc(loop);
  iterator i = std::lower_bound(B,E, assumption);
  return (i != E && *i == assumption );
}
bool SmtxSpeculationManager::isAssumedII(const Loop *loop, const Instruction *src, const Instruction *dst) const
{
  Assumption assumption(src,dst);
  iterator B = begin_ii(loop), E = end_ii(loop);
  iterator i = std::lower_bound(B,E, assumption);
  return (i != E && *i == assumption );
}

bool SmtxSpeculationManager::sourcesSpeculativelyRemovedEdge(const Loop *loop, const Instruction *src) const
{
  return sourcesSpeculativelyRemovedEdge(loop,lcNoFlow,src)
  ||     sourcesSpeculativelyRemovedEdge(loop,iiNoFlow,src);
}

bool SmtxSpeculationManager::sinksSpeculativelyRemovedEdge(const Loop *loop, const Instruction *dst) const
{
  return sinksSpeculativelyRemovedEdge(loop,lcNoFlow,dst)
  ||     sinksSpeculativelyRemovedEdge(loop,iiNoFlow,dst);
}

struct FindSource
{
  FindSource(const Instruction *s) : src(s) {}

  bool operator()(const SmtxSpeculationManager::Assumption &dep) const
  {
    return dep.src == src;
  }

private:
  const Instruction *src;
};

bool SmtxSpeculationManager::sourcesSpeculativelyRemovedEdge(const Loop *loop, const Loop2Assumptions &l2a, const Instruction *src) const
{
  Loop2Assumptions::const_iterator i = l2a.find(loop->getHeader() );
  if( i == l2a.end() )
    return false;

  const Assumptions &set = i->second;

  // Find the range [lo,hi) of assumptions s.t. assumption.src == src
  iterator begin = set.begin(), end = set.end();
  FindSource find(src);
  // The collection is sorted by 'dst', not 'src'
  // so we cannot use binary search here.
  iterator lo = std::find_if(begin, end, find);
  return lo != end;
}

struct FindDest
{
  bool operator()(const SmtxSpeculationManager::Assumption &dep, const Instruction *dst) const
  {
    return dep.dst < dst;
  }
};

bool SmtxSpeculationManager::sinksSpeculativelyRemovedEdge(const Loop *loop, const Loop2Assumptions &l2a, const Instruction *dst) const
{
  Loop2Assumptions::const_iterator i = l2a.find(loop->getHeader() );
  if( i == l2a.end() )
    return false;

  const Assumptions &set = i->second;

  // Find the range [lo,hi) of assumptions s.t. assumption.dst == dst
  iterator begin = set.begin(), end = set.end();
  FindDest find;
  iterator lo = std::lower_bound(begin, end, dst, find);
  if( lo == end )
    return false;
  if( lo->dst != dst )
    return false;

  // There is at least one assumption with the specified 'dst'
  return true;
}


// Insert 'elt' into 'list' unless it's already there.
static bool list_set_insert(SmtxSpeculationManager::Assumptions &list, const SmtxSpeculationManager::Assumption &elt)
{
  SmtxSpeculationManager::Assumptions::iterator B = list.begin(), E = list.end();
  SmtxSpeculationManager::Assumptions::iterator i = std::lower_bound(B,E, elt);
  if( i == E || *i != elt )
  {
    list.insert( i, elt );
    return true;
  }
  return false;
}

void SmtxSpeculationManager::setAssumedLC(const Loop *loop, const Instruction *src, const Instruction *dst)
{
  Assumption assumption(src,dst);
  Assumptions &list = getLC(loop);
  if( list_set_insert(list, assumption) )
    ++numAssumptionsLC;
}
void SmtxSpeculationManager::setAssumedII(const Loop *loop, const Instruction *src, const Instruction *dst)
{
  Assumption assumption(src,dst);
  Assumptions &list = getII(loop);
  if( list_set_insert(list, assumption) )
    ++numAssumptionsII;
}

template <class Type>
Type *map_elt(const ValueToValueMapTy &vmap, Type *tin)
{
  const ValueToValueMapTy::const_iterator j = vmap.find( tin );
  if( j == vmap.end() )
    return tin;

  else
    return cast< Type >( j->second );
}

static void map_assumptions(SmtxSpeculationManager::Loop2Assumptions &assumptions, const ValueToValueMapTy &vmap)
{
  SmtxSpeculationManager::Loop2Assumptions newAssumptions;
  for(SmtxSpeculationManager::Loop2Assumptions::const_iterator i=assumptions.begin(), e=assumptions.end(); i!=e; ++i)
  {
    const SmtxSpeculationManager::Assumptions &oldList = i->second;

    const BasicBlock *newHeader = map_elt(vmap, i->first);
    SmtxSpeculationManager::Assumptions &newList = newAssumptions[ newHeader ];
    for(unsigned j=0, N=oldList.size(); j<N; ++j)
    {
      const SmtxSpeculationManager::Assumption &oldPair = oldList[j];

      list_set_insert(newList,
        SmtxSpeculationManager::Assumption(
          map_elt(vmap, oldPair.src), map_elt(vmap, oldPair.dst) ));
    }
  }

  assumptions.swap(newAssumptions);
}

void SmtxSpeculationManager::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - SmtxSpeculationManager::contextRenamedViaClone: " << *changedContext << '\n';

  map_assumptions(iiNoFlow, vmap);
  map_assumptions(lcNoFlow, vmap);
}

void filter_anti_pipeline(const SmtxSpeculationManager::Assumptions &in, SmtxSpeculationManager::Assumptions &out, const PipelineStrategy &pipeline)
{
  for(unsigned i=0, N=in.size(); i<N; ++i)
  {
    const SmtxSpeculationManager::Assumption &ass = in[i];

    // Is this an anti-pipeline dep?
    if( pipeline.maybeAntiPipelineDependence(ass.src, ass.dst) )
      out.push_back(ass); // visiting assumptions in correct order, no need to insert-sort.
    else
      DEBUG(
        errs() << "Not anti-pipeline dep:\n"
               << "  from: " << *ass.src << '\n'
               << "    to: " << *ass.dst << '\n');
  }
}

static void filter_anti_parallel_stage(const SmtxSpeculationManager::Assumptions &in, SmtxSpeculationManager::Assumptions &out, const PipelineStrategy &pipeline)
{
  for(unsigned i=0, N=in.size(); i<N; ++i)
  {
    const SmtxSpeculationManager::Assumption &ass = in[i];

    if( pipeline.maybeAntiParallelStageDependence(ass.src, ass.dst) )
      out.push_back(ass); // visit assumptions in correct order, no need to insert-sort
    else
      DEBUG(
        errs() << "Not anti-parallel stage dep:\n"
               << "  from: " << *ass.src << '\n'
               << "    to: " << *ass.dst << '\n');

  }
}


void SmtxSpeculationManager::unspeculate(const Loop *loop, const PipelineStrategy &pipeline)
{
  Assumptions &lc = getLC(loop);
  Assumptions &ii = getII(loop);

  const unsigned before_lc = lc.size(),
                 before_ii = ii.size();

  DEBUG(
    const BasicBlock *header = loop->getHeader();
    const Function *fcn = header->getParent();
    errs() << "+++ Unspeculate "
           << "w.r.t. loop " << fcn->getName() << " :: " << header->getName()
           << ", starting with " << before_lc << " LC and " << before_ii << " II assumtions."
           << '\n';
  );

  Assumptions new_lc, new_ii;

  filter_anti_pipeline(lc, new_lc, pipeline);
  filter_anti_pipeline(ii, new_ii, pipeline);
  filter_anti_parallel_stage(lc, new_lc, pipeline);

  lc.swap( new_lc );
  ii.swap( new_ii );

  const unsigned after_lc = lc.size(),
                 after_ii = ii.size();

  DEBUG(
    errs() << "--- Unspeculate: finish with " << after_lc << " LC and " << after_ii << " II assumptions.";
  );

  numUnspeculatedLC += (before_lc-after_lc);
  numUnspeculatedII += (before_ii-after_ii);
}

void SmtxSpeculationManager::reset()
{
  iiNoFlow.clear();
  lcNoFlow.clear();
}

char SmtxSpeculationManager::ID = 0;
static RegisterPass< SmtxSpeculationManager > rp("spec-priv-smtx-manager", "SMTX manager");
}
}
