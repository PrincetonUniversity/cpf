#define DEBUG_TYPE "spec-priv-smtx-slamp-manager"

#include "liberty/SpecPriv/SmtxSlampManager.h"

#include "llvm/ADT/Statistic.h"

#include "liberty/Utilities/CallSiteFactory.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numAssumptionsLC, "Number of loop-carried no-flow assumptions");
STATISTIC(numAssumptionsII, "Number of intra-iteration no-flow assumptions");

STATISTIC(numUnspeculatedLC, "Number of UNspeculated loop-carried no-flow assumptions");
STATISTIC(numUnspeculatedII, "Number of UNspeculated intra-iteration no-flow assumptions");

static const SmtxSlampSpeculationManager::Assumptions empty_list;

bool SmtxSlampSpeculationManager::LinearPredictor::operator<(const LinearPredictor &other) const
{
  if ( is_double < other.is_double )
    return true;
  if ( is_double > other.is_double )
    return false;

  if ( context < other.context )
    return true;
  if ( context > other.context )
    return false;

  if( a < other.a)
    return true;
  if( a > other.a)
    return false;

  return b < other.b;
}

bool SmtxSlampSpeculationManager::LinearPredictor::operator==(const LinearPredictor &other) const
{
  return context == other.context && a == other.a && b == other.b && is_double == other.is_double;
}

bool SmtxSlampSpeculationManager::LinearPredictor::operator!=(const LinearPredictor &other) const
{
  return !((*this) == other);
}

// Order by the 'dst' element, ascending, and then by the 'src' element.
bool SmtxSlampSpeculationManager::Assumption::operator<(const Assumption &other) const
{
  if( dst < other.dst )
    return true;
  else if( dst > other.dst )
    return false;
  else
    return src < other.src;
}

bool SmtxSlampSpeculationManager::Assumption::operator==(const Assumption &other) const
{
  return src == other.src && dst == other.dst;
}

bool SmtxSlampSpeculationManager::Assumption::operator!=(const Assumption &other) const
{
  return !((*this) == other);
}

void SmtxSlampSpeculationManager::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< SLAMPLoadProfile >();
  au.setPreservesAll();
}

bool SmtxSlampSpeculationManager::runOnModule(Module &mod)
{
  slampResult = & getAnalysis< SLAMPLoadProfile >();

  return false;
}

const SmtxSlampSpeculationManager::Assumptions &SmtxSlampSpeculationManager::getLC(const Loop *loop) const
{
  Loop2Assumptions::const_iterator i = lcNoFlow.find( loop->getHeader() );
  if( i == lcNoFlow.end() )
    return empty_list;
  else
    return i->second;
}
const SmtxSlampSpeculationManager::Assumptions &SmtxSlampSpeculationManager::getII(const Loop *loop) const
{
  Loop2Assumptions::const_iterator i = iiNoFlow.find( loop->getHeader() );
  if( i == iiNoFlow.end() )
    return empty_list;
  else
    return i->second;
}

SmtxSlampSpeculationManager::Assumptions &SmtxSlampSpeculationManager::getLC(const Loop *loop)
{
  return lcNoFlow[ loop->getHeader() ];
}
SmtxSlampSpeculationManager::Assumptions &SmtxSlampSpeculationManager::getII(const Loop *loop)
{
  return iiNoFlow[ loop->getHeader() ];
}

SmtxSlampSpeculationManager::iterator SmtxSlampSpeculationManager::begin_lc(const Loop *loop) const
{
  return getLC(loop).begin();
}
SmtxSlampSpeculationManager::iterator SmtxSlampSpeculationManager::end_lc(const Loop *loop) const
{
  return getLC(loop).end();
}

SmtxSlampSpeculationManager::iterator SmtxSlampSpeculationManager::begin_ii(const Loop *loop) const
{
  return getII(loop).begin();
}
SmtxSlampSpeculationManager::iterator SmtxSlampSpeculationManager::end_ii(const Loop *loop) const
{
  return getII(loop).end();
}

bool SmtxSlampSpeculationManager::isAssumedLC(const Loop *loop, const Instruction *src, const Instruction *dst) const
{
  Assumption assumption(src,dst);
  iterator B = begin_lc(loop), E = end_lc(loop);
  iterator i = std::lower_bound(B,E, assumption);
  return (i != E && *i == assumption );
}
bool SmtxSlampSpeculationManager::isAssumedII(const Loop *loop, const Instruction *src, const Instruction *dst) const
{
  Assumption assumption(src,dst);
  iterator B = begin_ii(loop), E = end_ii(loop);
  iterator i = std::lower_bound(B,E, assumption);
  return (i != E && *i == assumption );
}

bool SmtxSlampSpeculationManager::sourcesSpeculativelyRemovedEdge(const Loop *loop, const Instruction *src) const
{
  return sourcesSpeculativelyRemovedEdge(loop,lcNoFlow,src)
  ||     sourcesSpeculativelyRemovedEdge(loop,iiNoFlow,src);
}

bool SmtxSlampSpeculationManager::sinksSpeculativelyRemovedEdge(const Loop *loop, const Instruction *dst) const
{
  return sinksSpeculativelyRemovedEdge(loop,lcNoFlow,dst)
  ||     sinksSpeculativelyRemovedEdge(loop,iiNoFlow,dst);
}

bool SmtxSlampSpeculationManager::useLoopInvariantPrediction(Loop* loop, const Instruction* dst) const
{
  if (!loop) return false;

#if 0
  BasicBlock* header = loop->getHeader();

  if (const LoadInst* li = dyn_cast<LoadInst>(dst))
  {
    if (!ctxts_per_load.count(header))
      return false;

    Load2ContextIDs load2ctxtids = ctxts_per_load.at(header);

    if (!load2ctxtids.count(li))
      return false;

    const set<unsigned>& s = load2ctxtids.at(li);

    return !s.empty();
  }
  return false;
#endif

  if (const LoadInst* li = dyn_cast<LoadInst>(dst))
  {
    if (!ctxts_per_load.count(li))
      return false;

    const set<unsigned>& s = ctxts_per_load.at(li);

    return !s.empty();
  }
  return false;
}

bool SmtxSlampSpeculationManager::useLinearPredictor(Loop* loop, const Instruction* dst) const
{
#if 0
  if (!loop) return false;

  const BasicBlock* header = loop->getHeader();

  if ( !loop2load2lps.count(header) )
    return false;

  const Load2LPs& load2lps = loop2load2lps.at(header);
#endif

  if (const LoadInst* li = dyn_cast<LoadInst>(dst))
    return load2lps.count(li);

  return false;
}

struct FindSource
{
  FindSource(const Instruction *s) : src(s) {}

  bool operator()(const SmtxSlampSpeculationManager::Assumption &dep) const
  {
    return dep.src == src;
  }

private:
  const Instruction *src;
};

bool SmtxSlampSpeculationManager::sourcesSpeculativelyRemovedEdge(const Loop *loop, const Loop2Assumptions &l2a, const Instruction *src) const
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
  bool operator()(const SmtxSlampSpeculationManager::Assumption &dep, const Instruction *dst) const
  {
    return dep.dst < dst;
  }
};

bool SmtxSlampSpeculationManager::sinksSpeculativelyRemovedEdge(const Loop *loop, const Loop2Assumptions &l2a, const Instruction *dst) const
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
static bool list_set_insert(SmtxSlampSpeculationManager::Assumptions &list, const SmtxSlampSpeculationManager::Assumption &elt)
{
  SmtxSlampSpeculationManager::Assumptions::iterator B = list.begin(), E = list.end();
  SmtxSlampSpeculationManager::Assumptions::iterator i = std::lower_bound(B,E, elt);
  if( i == E || *i != elt )
  {
    list.insert( i, elt );
    return true;
  }
  return false;
}

unsigned SmtxSlampSpeculationManager::registerContext(const Loop* loop, const CallInst* callinst)
{
#if 0
  Contexts& contexts_for_loop = all_ctxts[loop->getHeader()];

  if ( !contexts_for_loop.count(callinst) )
  {
    unsigned newid = contexts_for_loop.size() + 1;
    contexts_for_loop[callinst] = newid;
  }

  unsigned id = contexts_for_loop[callinst];
  assert(id);
  return id;
#endif
  if ( !unique_ctxts.count(callinst) )
  {
    unsigned newid = unique_ctxts.size() + 1;
    unique_ctxts[callinst] = newid;
  }

  unsigned id = unique_ctxts[callinst];
  assert(id);
  return id;
}

void SmtxSlampSpeculationManager::addLoopInvariantPredictableContext(
  const Loop* loop, const LoadInst* load, unsigned ctxt_id
)
{
#if 0
  Load2ContextIDs& load2ctxtids = ctxts_per_load[loop->getHeader()];
#endif
  ctxts_per_load[load].insert(ctxt_id);

  // sanity check

  if (ctxt_id == 0)
  {
    assert(ctxts_per_load[load].size() == 1);
  }
}

void SmtxSlampSpeculationManager::setAssumedLC(
  const Loop *loop, const Instruction *src, const Instruction *dst,
  const Instruction* context
)
{
  Assumption assumption(src,dst);
  Assumptions &list = getLC(loop);
  if( list_set_insert(list, assumption) )
    ++numAssumptionsLC;

  if (context)
  {
    unsigned ctxt_id = 0;

    if (const CallInst* ci = dyn_cast<CallInst>(context))
    {
      ctxt_id = registerContext(loop, ci);
    }

    const LoadInst* li = dyn_cast<LoadInst>(dst);
    assert(li);

    addLoopInvariantPredictableContext(loop, li, ctxt_id);
  }
}

void SmtxSlampSpeculationManager::setAssumedLC(
  const Loop *loop, const Instruction *src, const LoadInst* dst,
  const Instruction* context, int64_t a, int64_t b, bool is_double
)
{
  Assumption assumption(src,dst);
  Assumptions &list = getLC(loop);
  if( list_set_insert(list, assumption) )
    ++numAssumptionsLC;

#if 0
  Load2LPs& load2lps = loop2load2lps[loop->getHeader()];

  if ( !load2lps.count(dst) )
  {
    LinearPredictors lps;
    load2lps[dst] = lps;
  }
#endif

  LinearPredictors& lps = load2lps[dst];

  unsigned ctxt_id = 0;

  if (const CallInst* ci = dyn_cast<CallInst>(context))
  {
    ctxt_id = registerContext(loop, ci);
  }

  // sanity check

  for (LinearPredictors::iterator i = lps.begin() ; i != lps.end() ; i++)
  {
    if (i->context == ctxt_id)
    {
      assert(i->a == a && i->b == b && i->is_double == is_double);
    }
  }

  lps.insert( LinearPredictor(ctxt_id, a, b, is_double) );
}

void SmtxSlampSpeculationManager::setAssumedII(
  const Loop *loop, const Instruction *src, const Instruction *dst,
  const Instruction* context)
{
  // loop-invariant prediction cannot be applied to intra-iteration dependencies

  assert(context == NULL);

  Assumption assumption(src,dst);
  Assumptions &list = getII(loop);
  if( list_set_insert(list, assumption) )
    ++numAssumptionsII;
}

void SmtxSlampSpeculationManager::setAssumedII(
  const Loop *loop, const Instruction *src, const LoadInst* dst,
  const Instruction* context, int64_t a, int64_t b, bool is_double
)
{
  Assumption assumption(src,dst);
  Assumptions &list = getII(loop);
  if( list_set_insert(list, assumption) )
    ++numAssumptionsII;

#if 0
  BasicBlock* header = loop->getHeader();
  Load2LPs& load2lps = loop2load2lps[loop->getHeader()];

  if (!load2lps.count(dst))
  {
    LinearPredictors lps;
    load2lps[dst] = lps;
  }

  LinearPredictors& lps = load2lps[dst];

  unsigned ctxtid = 0;

  if (const CallInst* ci = dyn_cast<CallInst>(context))
  {
    Contexts& ctxts = all_ctxts[header];

    if ( !ctxts.count(ci) )
    {
      ctxts[ci] = ctxts.size()+1;
    }

    ctxtid = ctxts[ci];
    assert(ctxtid);
  }
#endif

  LinearPredictors& lps = load2lps[dst];

  unsigned ctxtid = 0;

  if (const CallInst* ci = dyn_cast<CallInst>(context))
  {
    ctxtid = registerContext(loop, ci);
  }

  // sanity check

  for (LinearPredictors::iterator i = lps.begin() ; i != lps.end() ; i++)
  {
    if (i->context == ctxtid) assert(i->a == a && i->b == b && i->is_double == is_double);
  }

  lps.insert( LinearPredictor(ctxtid, a, b, is_double) );
}

set<unsigned> SmtxSlampSpeculationManager::getLIPredictionApplicableCtxts(Loop* loop, LoadInst* dst)
{
#if 0
  assert( ctxts_per_load.count(loop->getHeader()) );

  Load2ContextIDs& load2ctxtids = ctxts_per_load.at(loop->getHeader());
  assert( load2ctxtids.count(dst) );

  return load2ctxtids.at(dst);
#endif
  assert( ctxts_per_load.count(dst) );
  return ctxts_per_load.at(dst);
}

SmtxSlampSpeculationManager::LinearPredictors& SmtxSlampSpeculationManager::getLinearPredictors(Loop* loop, const LoadInst* dst)
{
#if 0
  assert( loop2load2lps.count(loop->getHeader()) );

  Load2LPs& load2lps = loop2load2lps.at(loop->getHeader());
#endif
  assert( load2lps.count(dst) );

  return load2lps.at(dst);
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

static void map_assumptions(SmtxSlampSpeculationManager::Loop2Assumptions &assumptions, const ValueToValueMapTy &vmap)
{
  SmtxSlampSpeculationManager::Loop2Assumptions newAssumptions;
  for(SmtxSlampSpeculationManager::Loop2Assumptions::const_iterator i=assumptions.begin(), e=assumptions.end(); i!=e; ++i)
  {
    const SmtxSlampSpeculationManager::Assumptions &oldList = i->second;

    const BasicBlock *newHeader = map_elt(vmap, i->first);
    SmtxSlampSpeculationManager::Assumptions &newList = newAssumptions[ newHeader ];
    for(unsigned j=0, N=oldList.size(); j<N; ++j)
    {
      const SmtxSlampSpeculationManager::Assumption &oldPair = oldList[j];

      list_set_insert(newList,
        SmtxSlampSpeculationManager::Assumption(
          map_elt(vmap, oldPair.src), map_elt(vmap, oldPair.dst) ));
    }
  }

  assumptions.swap(newAssumptions);
}

void SmtxSlampSpeculationManager::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - SmtxSlampSpeculationManager::contextRenamedViaClone: " << *changedContext << '\n';

  map_assumptions(iiNoFlow, vmap);
  map_assumptions(lcNoFlow, vmap);
}

void filter_anti_pipeline(const SmtxSlampSpeculationManager::Assumptions &in, SmtxSlampSpeculationManager::Assumptions &out, const PipelineStrategy &pipeline)
{
  for(unsigned i=0, N=in.size(); i<N; ++i)
  {
    const SmtxSlampSpeculationManager::Assumption &ass = in[i];

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

static void filter_anti_parallel_stage(const SmtxSlampSpeculationManager::Assumptions &in, SmtxSlampSpeculationManager::Assumptions &out, const PipelineStrategy &pipeline)
{
  for(unsigned i=0, N=in.size(); i<N; ++i)
  {
    const SmtxSlampSpeculationManager::Assumption &ass = in[i];

    if( pipeline.maybeAntiParallelStageDependence(ass.src, ass.dst) )
      out.push_back(ass); // visit assumptions in correct order, no need to insert-sort
    else
      DEBUG(
        errs() << "Not anti-parallel stage dep:\n"
               << "  from: " << *ass.src << '\n'
               << "    to: " << *ass.dst << '\n');

  }
}

void SmtxSlampSpeculationManager::unspeculate(const Loop *loop, const PipelineStrategy &pipeline)
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

void SmtxSlampSpeculationManager::reset()
{
  iiNoFlow.clear();
  lcNoFlow.clear();
}

void SmtxSlampSpeculationManager::collectWrites(Function* fcn, std::vector<const Instruction*>& writes)
{
  if (!fcn) return;

  if ( !fcn2writes.count(fcn) )
  {
    set<BasicBlock*> bbs;
    sweep(fcn, bbs);

    set<StoreInst*> stores;
    set<LoadInst*>  loads;

    for (set<BasicBlock*>::iterator bi = bbs.begin() ; bi != bbs.end() ; bi++)
    {
      BasicBlock* bb = *bi;
      for (BasicBlock::iterator ii = bb->begin() ; ii != bb->end() ; ii++)
      {
        if (StoreInst* si = dyn_cast<StoreInst>(&*ii))
          stores.insert(si);
        if (LoadInst* li = dyn_cast<LoadInst>(&*ii))
          loads.insert(li);
      }
    }

    fcn2writes[fcn].insert(stores.begin(), stores.end());
    fcn2reads[fcn].insert(loads.begin(), loads.end());
  }

  set<StoreInst*>& s = fcn2writes[fcn];
  copy(s.begin(), s.end(), back_inserter(writes));
}

void SmtxSlampSpeculationManager::collectReads(Function* fcn, std::vector<const Instruction*>& reads)
{
  if (!fcn) return;
  if ( !fcn2reads.count(fcn) )
  {
    set<BasicBlock*> bbs;
    sweep(fcn, bbs);

    set<StoreInst*> stores;
    set<LoadInst*>  loads;

    for (set<BasicBlock*>::iterator bi = bbs.begin() ; bi != bbs.end() ; bi++)
    {
      BasicBlock* bb = *bi;
      for (BasicBlock::iterator ii = bb->begin() ; ii != bb->end() ; ii++)
      {
        if (StoreInst* si = dyn_cast<StoreInst>(&*ii))
          stores.insert(si);
        if (LoadInst* li = dyn_cast<LoadInst>(&*ii))
          loads.insert(li);
      }
    }

    fcn2writes[fcn].insert(stores.begin(), stores.end());
    fcn2reads[fcn].insert(loads.begin(), loads.end());
  }

  set<LoadInst*>& s = fcn2reads[fcn];
  copy(s.begin(), s.end(), back_inserter(reads));
}

void SmtxSlampSpeculationManager::sweep(Function::iterator begin, Function::iterator end, set<BasicBlock*>& bbs)
{
  for(Function::iterator i=begin; i!=end; ++i)
    sweep( &*i, bbs );
}

void SmtxSlampSpeculationManager::sweep(BasicBlock* bb, set<BasicBlock*>& bbs)
{
  if( bbs.count(bb) )
    return;
  bbs.insert(bb);

  for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
  {
    CallSite cs = getCallSite(&*i);
    if( !cs.getInstruction() )
      continue;

    Function *callee = cs.getCalledFunction();
    if( !callee )
      continue;

    if( callee->isDeclaration() )
      continue;

    sweep(callee->begin(), callee->end(), bbs);
  }
}

void SmtxSlampSpeculationManager::sweep(Function* fcn, set<BasicBlock*>& bbs)
{
  sweep(fcn->begin(), fcn->end(), bbs);
}

char SmtxSlampSpeculationManager::ID = 0;
static RegisterPass< SmtxSlampSpeculationManager > rp("spec-priv-smtx-slamp-manager", "SLAMP based SMTX manager");
}
}
