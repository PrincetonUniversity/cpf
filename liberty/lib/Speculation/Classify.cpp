#define DEBUG_TYPE "classify"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Analysis/CallsiteDepthCombinator.h"
#include "liberty/Analysis/CallsiteSearch.h"
#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/Analysis/KillFlow.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LAMP/LampOracleAA.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/PointsToAA.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/StableHash.h"
#include "liberty/Utilities/Timer.h"

#include <set>

namespace liberty
{
template <>
stable_hash_code stable_hash<SpecPriv::Ctx const&>(SpecPriv::Ctx const &ctx)
{
  return stable_combine((int)ctx.type, ctx.fcn, ctx.header, ctx.parent);
}

template <>
stable_hash_code stable_hash<SpecPriv::AU const&>(SpecPriv::AU const &au)
{
  return stable_combine((int)au.type, au.value, au.ctx);
}

namespace SpecPriv
{
using namespace llvm;

STATISTIC(numClassified, "Parallel regions selected #regression");

static cl::opt<bool> PrintFootprints(
  "print-loop-footprints", cl::init(false), cl::NotHidden,
  cl::desc("Print memory footprint of each hot loop"));
static cl::opt<unsigned> NumSubHeaps(
  "num-subheaps", cl::init(8), cl::NotHidden,
  cl::desc("Sub-divide heaps into N subheaps"));


void Classify::getAnalysisUsage(AnalysisUsage &au) const
{
  //au.addRequired< DataLayout >();
  au.addRequired< TargetLibraryInfoWrapperPass >();
  au.addRequired< ModuleLoops >();
  au.addRequired< LAMPLoadProfile >();
  au.addRequired< ReadPass >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< ProfileGuidedPredictionSpeculator >();
  au.addRequired< LoopAA >();
  au.addRequired< KillFlow >();
  au.addRequired< Targets >();
  au.setPreservesAll();
}


bool Classify::runOnModule(Module &mod)
{
  DEBUG(errs() << "#################################################\n"
               << " Classification\n\n\n");
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Targets &targets = getAnalysis< Targets >();

  // We augment the LoopAA stack with all
  // of the speculative AAs...

  {
    // CtrlSpec
    ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
    EdgeCountOracle edgeaa(ctrlspec);
    edgeaa.InitializeLoopAA(this, mod.getDataLayout());
    // LAMP
    LAMPLoadProfile &lamp = getAnalysis< LAMPLoadProfile >();
    LampOracle lampaa(&lamp);
    lampaa.InitializeLoopAA(this, mod.getDataLayout());
    // Points-to
    const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
    PointsToAA pointstoaa(spresults);
    pointstoaa.InitializeLoopAA(this, mod.getDataLayout());
    // Value predictions
    ProfileGuidedPredictionSpeculator &predspec = getAnalysis< ProfileGuidedPredictionSpeculator >();
    PredictionAA predaa(&predspec);
    predaa.InitializeLoopAA(this, mod.getDataLayout());

    // Run on each loop.
    for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i)
      TIME("Classify loop", runOnLoop(*i));

    // Those four AAs remove themselves from
    // the stack automatically as they are
    // destructed HERE.
  }

  return false;
}




static const Ctx *translateContexts(const Read &spresults, const Ctx *root, const Context &context)
{
  // Translate a callsite-search ctx into a spec-priv ctx
  const Ctx *cc = root;
  const CallsiteContext *csc = context.front();
  while( csc )
  {
    cc = spresults.getCtx( csc->getFunction(), cc );
    csc = csc->getParent();
  }

  return cc;
}

static bool intersect_into(const AUs &a, const AUs &b, AUs &out)
{
  const unsigned size_in = out.size();

  // Intersection, ignoring NULL
  for(AUs::const_iterator i=a.begin(), e=a.end(); i!=e; ++i)
  {
    AU *au1 = *i;
    if( au1->type == AU_Null )
      continue;

    for(AUs::const_iterator j=b.begin(), f=b.end(); j!=f; ++j)
    {
      AU *au2 = *j;
      if( au2->type == AU_Null )
        continue;

      if( (*au1) == (*au2) )
      {
        // We only want to report it if we haven't already added it.
        if( std::find(out.begin() /*+size_in*/, out.end(), au1) != out.end() )
          continue;

        out.push_back( au1 );
      }
    }
  }

  return (out.size() > size_in);
}

static void union_into(const ReduxAUs &a, AUs &out)
{
  for(ReduxAUs::const_iterator i=a.begin(), e=a.end(); i!=e; ++i)
  {
    AU *au = i->first;
    out.push_back(au);
  }
}

static void strip_undefined_objects(AUs &out)
{
  for(unsigned i=0; i<out.size(); ++i)
  {
    if( out[i]->type == AU_Undefined )
    {
      DEBUG(errs() << "N.B. Removed an UNDEFINED object, at my discretion\n");
      std::swap( out[i], out.back() );
      out.pop_back();
      --i;
    }
  }
}

static void strip_undefined_objects(HeapAssignment::AUSet &out)
{
  for(HeapAssignment::AUSet::iterator i=out.begin(); i!=out.end();)
  {
    AU *au = *i;
    if( au->type == AU_Undefined )
    {
      DEBUG(errs() << "N.B. Removed an UNDEFINED object, at my discretion\n");
      out.erase(i);
      i = out.begin();
    }
    else
      ++i;
  }
}

static void strip_undefined_objects(HeapAssignment::ReduxAUSet &out)
{
  for(HeapAssignment::ReduxAUSet::iterator i=out.begin(); i!=out.end();)
  {
    AU *au = i->first;
    if( au->type == AU_Undefined )
    {
      DEBUG(errs() << "N.B. Removed an UNDEFINED object, at my discretion\n");
      out.erase(i);
      i = out.begin();
    }
    else
      ++i;
  }
}


bool Classify::getLoopCarriedAUs(Loop *loop, const Ctx *ctx, AUs &aus) const
{
  KillFlow &kill = getAnalysis< KillFlow >();
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(loop->getHeader());

  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *srcbb = *i;
    if( ctrlspec->isSpeculativelyDead(srcbb) )
      continue;

    for(BasicBlock::iterator j=srcbb->begin(), f=srcbb->end(); j!=f; ++j)
    {
      Instruction *src = &*j;
      if( !src->mayWriteToMemory() )
        continue;

      // Re-use this to speed it all up.
      ReverseStoreSearch search_src(src,kill);

      for(Loop::block_iterator k=loop->block_begin(); k!=e; ++k)
      {
        BasicBlock *dstbb = *k;
        if( ctrlspec->isSpeculativelyDead(dstbb) )
          continue;

        for(BasicBlock::iterator l=dstbb->begin(), h=dstbb->end(); l!=h; ++l)
        {
          Instruction *dst = &*l;
          if( !dst->mayReadFromMemory() )
            continue;

          // There may be a cross-iteration flow from src to dst.
          if( !getUnderlyingAUs(loop, search_src,src,ctx, dst,ctx, aus) )
            return false;
        }
      }
    }
  }
  return true;
}

// Case where one or both instructions are an aggregation of
// several operations.  In this case, we want to know specifically
// which instructions caused the dependence, not assume that
// all did.
bool Classify::getUnderlyingAUs(Loop *loop, ReverseStoreSearch &search_src, Instruction *src, const Ctx *src_ctx, Instruction *dst, const Ctx *dst_ctx, AUs &aus) const
{
  KillFlow &kill = getAnalysis< KillFlow >();

  CCPairs flows;
  CallsiteDepthCombinator::doFlowSearchCrossIter(src, dst, loop, search_src, kill, &flows);
  if( flows.empty() )
    return true;

  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(loop->getHeader());

  for(CCPairs::const_iterator i=flows.begin(), e=flows.end(); i!=e; ++i)
  {
    const CtxInst srcp = i->first;
    const CtxInst dstp = i->second;

    if( ctrlspec->isSpeculativelyDead(srcp) )
      continue;
    if( ctrlspec->isSpeculativelyDead(dstp) )
      continue;

    if( !getUnderlyingAUs(srcp,src_ctx, dstp,dst_ctx, aus) )
      return false;
  }
  return true;
}

bool Classify::getUnderlyingAUs(const CtxInst &src, const Ctx *src_ctx, const CtxInst &dst, const Ctx *dst_ctx, AUs &aus) const
{
  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();

  const Ctx *src_cc = translateContexts(spresults, src_ctx, src.getContext());
  const Instruction *srci = src.getInst();

  // Footprint of operation ci
  // It's valid to look only at the WRITE footprint,
  // because we're talking about a flow dep.
  AUs ri, wi;
  ReduxAUs xi;
  if( ! spresults.getFootprint(srci,src_cc,ri,wi,xi) )
  {
    errs() << "Failed to get write footprint for: " << src << '\n';
    return false;
  }

  union_into(xi,wi);

  // We are allowed to do whatever with undefined behavior.
  strip_undefined_objects(wi);

  if( wi.empty() )
    return true;

  const Ctx *dst_cc = translateContexts(spresults, dst_ctx, dst.getContext());
  const Instruction *dsti = dst.getInst();;

  AUs rj, wj;
  ReduxAUs xj;
  if( !spresults.getFootprint(dsti,dst_cc,rj,wj,xj) )
  {
    errs() << "Failed to get read footprint for: " << dst << '\n';
    return false;
  }

  // rj ||= xj
  union_into(xj, rj);

  // We are allowed to do whatever with undefined behavior.
  // I choose to remove them.
  strip_undefined_objects(rj);

  if( rj.empty() )
    return true;

  const unsigned size_before = aus.size();
  intersect_into(wi, rj, aus);

  // Print the new AUs.
  DEBUG(
    const unsigned N = aus.size();
    if( N > size_before )
    {
      bool first = true;
      for(unsigned i=size_before; i<N; ++i)
      {
        AU *au1 = aus[i];

        if( first )
          errs() << "( ";
        else
          errs() << ", ";

        errs() << *au1;
        first = false;
      }

      errs() << " )\n"
             << "There is a flow from:\n" << src << "\nto:\n" << dst << "\n\n";
    }
  );

  return true;
}


bool Classify::runOnLoop(Loop *loop)
{
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
  if( !spresults.resultsValid() )
    return false;

  DEBUG(errs() << "***************** Classify: "
    << fcn->getName() << " :: " << header->getName()
    << " *****************\n");

  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(loop->getHeader());
  if( ctrlspec->isNotLoop(loop) )
  {
    DEBUG(errs() << "- This loop never takes its backedge.\n");
    return false;
  }

  // Build the assignment
  HeapAssignment &assignment = assignments[header];
  HeapAssignment::AUSet
        &sharedAUs = assignment.getSharedAUs(),
        &localAUs = assignment.getLocalAUs(),
        &privateAUs = assignment.getPrivateAUs(),
        &readOnlyAUs = assignment.getReadOnlyAUs();
  HeapAssignment::ReduxAUSet &reductionAUs = assignment.getReductionAUs();

  const Ctx *ctx = spresults.getCtx(loop);

  // Find all AUs which are read,written,reduced...
  AUs reads, writes;
  ReduxAUs reductions;
  if( !spresults.getFootprint(loop, ctx, reads, writes, reductions) )
  {
    DEBUG(errs() << "Classify: Failed to get write footprint of loop; abort\n");
    return false;
  }

  // {{{ Debug
  if( PrintFootprints )
  {
    typedef std::set<AU*> AUSet;
    typedef std::set<ReduxAU> ReduxAUSet;

    if( ! reads.empty() )
    {
      AUSet readSet(reads.begin(),reads.end());
      errs() << "---Loop footprint---\n"
             << "  Reads (" << readSet.size() << "):\n";
      for(AUSet::const_iterator i=readSet.begin(), e=readSet.end(); i!=e; ++i)
        errs() << "   " << **i << '\n';
    }

    if( ! writes.empty() )
    {
      AUSet writeSet(writes.begin(),writes.end());
      errs() << "  Writes (" << writeSet.size() << "):\n";
      for(AUSet::const_iterator i=writeSet.begin(), e=writeSet.end(); i!=e; ++i)
        errs() << "   " << **i << '\n';
    }

    if( ! reductions.empty() )
    {
      ReduxAUSet reductionSet( reductions.begin(), reductions.end() );
      errs() << "  Redux (" << reductionSet.size() << "):\n";
      for(ReduxAUSet::const_iterator i=reductionSet.begin(), e=reductionSet.end(); i!=e; ++i)
        errs() << "   " << i->second << " . " << *(i->first) << '\n';
    }

    errs() << "---End footprint---\n";
  }
  // }}}

  // Find local AUs first.
  for(AUs::const_iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
  {
    AU *au = *i;

    // Is this AU local?
    const Read::Ctx2Count &locals = spresults.find_locals(au);
    for(Read::Ctx2Count::const_iterator j=locals.begin(), f=locals.end(); j!=f; ++j)
      if( j->first->matches(ctx) )
      {
        localAUs.insert(au);
        break;
      }
  }

  // reductionAUs = reductions \ locals
  // Eliminate inconsistent reductions
  HeapAssignment::AUSet inconsistent;
  for(ReduxAUs::iterator i=reductions.begin(), e=reductions.end(); i!=e; ++i)
  {
    AU *au = i->first;
    if( inconsistent.count( au ) )
      continue;
    if( localAUs.count( au ) )
      continue;

    Reduction::Type rt = i->second;

    HeapAssignment::ReduxAUSet::iterator j=reductionAUs.find(au);
    if( j != reductionAUs.end() && j->second != rt )
    {
      DEBUG(errs() << "Not redux: au " << *au
                   << " is sometimes " << Reduction::names[rt]
                   << " but other times " << Reduction::names[j->second] << '\n');
      inconsistent.insert(au);
      reductionAUs.erase(j);
    }

    else
      reductionAUs[ au ] = rt;
  }
  // reductionAUs = reductionAUs \ reads
  for(AUs::const_iterator i=reads.begin(), e=reads.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( reductionAUs.count(au) )
    {
      reductionAUs.erase( au );

      // since the reduction entry
      // counted as both a read and a write.
      writes.push_back( au );
    }
  }

  // reductionAUs = reductionAUs \ writes
  for(AUs::const_iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( reductionAUs.count(au) )
    {
      reductionAUs.erase( au );

      // since the reduction entry counted
      // as both a read and a write
      reads.push_back( au );
    }
  }

  // For each pair (write, read) in the loop.
  AUs loopCarried;
  if( !getLoopCarriedAUs(loop, ctx, loopCarried) )
  {
    DEBUG(errs() << "Wild object spoiled classification.\n");
    return false;
  }

  for(AUs::const_iterator i=loopCarried.begin(), e=loopCarried.end(); i!=e; ++i)
    if( !localAUs.count( *i ) )
      if( !reductionAUs.count( *i ) )
        sharedAUs.insert( *i );

  // AUs which are written during the loop, but which
  // are not local, shared or reduction, are privatized.
  for(AUs::const_iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
  {
    AU *au = *i;

    // Is this AU local, shared or redux?
    if( localAUs.count(au) )
      continue;
    if( sharedAUs.count(au) )
      continue;
    if( reductionAUs.count(au) )
      continue;

    // Otherwise, it is private.
    privateAUs.insert(au);
  }

  // AUs which are read, but which are not
  // local, shared, reduction are read-only.
  for(AUs::const_iterator i=reads.begin(), e=reads.end(); i!=e; ++i)
  {
    AU *au = *i;

    if( localAUs.count(au) )
      continue;
    else if( sharedAUs.count(au) )
      continue;
    if( reductionAUs.count(au) )
      continue;
    else if( privateAUs.count(au) )
      continue;

    // Is this AU local?
    bool isLocal = false;
    const Read::Ctx2Count &locals = spresults.find_locals(au);
    for(Read::Ctx2Count::const_iterator j=locals.begin(), f=locals.end(); j!=f; ++j)
      if( j->first->matches(ctx) )
      {
        isLocal = true;
        break;
      }

    if( isLocal )
      localAUs.insert(au);
    else
      readOnlyAUs.insert(au);
  }

  // Strip the undefined AU
  strip_undefined_objects( sharedAUs );
  strip_undefined_objects( localAUs );
  strip_undefined_objects( privateAUs );
  strip_undefined_objects( readOnlyAUs );
  strip_undefined_objects( reductionAUs );

  assignment.assignSubHeaps();

  assignment.setValidFor(loop);
  ++numClassified;
  DEBUG( errs() << assignment );

  return false;
}

template <class In>
static AU *getAU(In in)
{
  return 0;
}

template <>
AU *getAU<AU*>(AU *in)
{
  return in;
}

template <>
AU *getAU<HeapAssignment::ReduxAUSet::value_type&>(HeapAssignment::ReduxAUSet::value_type &pair)
{
  return pair.first;
}

template <class Collection>
void HeapAssignment::assignSubHeaps(Collection &c)
{
  // I don't yet understand this opportunity fully,
  // and so I'll invent a heuristic.
  // The design constraints of this heuristic
  // are:
  //  - Repeatability: if we run the compiler several
  //    times, we want the same sub-heap assignment.
  //
  // The heuristic is:
  // - (Hash the object's name) mod N.
  for(typename Collection::iterator i=c.begin(), e=c.end(); i!=e; ++i)
  {
    const AU *au = getAU(*i);
    stable_hash_code h = stable_hash(au);
    setSubHeap(au, h % NumSubHeaps);
  }
}

void HeapAssignment::assignSubHeaps()
{
  // Assign objects to sub-heaps.
  assignSubHeaps( getSharedAUs() );
  assignSubHeaps( getLocalAUs() );
  assignSubHeaps( getPrivateAUs() );
  // (don't bother with the read-only heap)
  assignSubHeaps( getReductionAUs() );
}

bool Classify::isAssigned(const Loop *L) const
{
  return assignments.count( L->getHeader() );
}

const HeapAssignment &Classify::getAssignmentFor(const Loop *L) const
{
  Loop2Assignments::const_iterator i = assignments.find( L->getHeader() );
  assert( i != assignments.end() && "No assignment available for this loop");

  return i->second;
}

char Classify::ID = 0;
static RegisterPass<Classify> x("spec-priv-classify",
 "Classify all AUs as one of LOCAL, PRIVATE, REDUCTION, SHARED or READ-ONLY", false,false);

HeapAssignment::Type HeapAssignment::join(Type a, Type b)
{
  if( a == b )
    return a;

  // This is a symmetric operator.
  if( a > b )
    std::swap(a,b);

  if( a == ReadOnly && b != Redux )
    return b;

  if( a == Local && b == Private )
    return Private;

  return Unclassified;
}

// {{{ Printing
void HeapAssignment::print(raw_ostream &fout) const
{
  std::string name;

  fout << "Classification report:";
  if( success.empty() )
  {
    fout << " Not valid.\n";
    name = "(invalid)";
  }
  else
  {
    fout << " Valid for: ";
    for(LoopSet::const_iterator i=success.begin(), e=success.end(); i!=e; ++i)
    {
      const BasicBlock *header = *i;
      const Function *fcn = header->getParent();

      fout << "  Loop " << fcn->getName() << " :: " << header->getName() << ", ";
      name = ("(" + fcn->getName() + " :: " + header->getName() + ")").str();
    }
    fout << '\n';

    if( success.size() > 1 )
      name = "(combined)";
  }

  fout << "  Found " << shareds.size() << " shared AUs:\n";
  for(AUSet::const_iterator i=shareds.begin(), e=shareds.end(); i!=e; ++i)
  {
    AU *au = *i;
    fout << "    o shared";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << locals.size() << " local AUs:\n";
  for(AUSet::const_iterator i=locals.begin(), e=locals.end(); i!=e; ++i)
  {
    AU *au = *i;
    fout << "    o local";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << reduxs.size() << " reduction AUs:\n";
  for(ReduxAUSet::const_iterator i=reduxs.begin(), e=reduxs.end(); i!=e; ++i)
  {
    AU *au = i->first;
    Reduction::Type rt = i->second;
    fout << "    o redux("<< Reduction::names[rt] <<")";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << privs.size() << " private AUs:\n";
  for(AUSet::const_iterator i=privs.begin(), e=privs.end(); i!=e; ++i)
  {
    AU *au = *i;
    fout << "    o priv";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << ros.size() << " read-only (live-in) AUs:\n";
  for(AUSet::const_iterator i=ros.begin(), e=ros.end(); i!=e; ++i)
  {
    AU *au = *i;
    fout << "    o ro";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
}

void HeapAssignment::dump() const
{
  print( errs() );
}

raw_ostream &operator<<(raw_ostream &fout, const HeapAssignment &assg)
{
  assg.print(fout);
  return fout;
}
// }}}

int HeapAssignment::getSubHeap(const AU *au) const
{
  SubheapAssignment::const_iterator i = subheaps.find(au);
  if( i == subheaps.end() )
    return -1;

  return i->second;
}

int HeapAssignment::getSubHeap(const Value *ptr, const Loop *loop, const Read &spresults) const
{
  // Map ptr to AUs.
  Ptrs aus;
  const Ctx *ctx = spresults.getCtx(loop);
  if( ! spresults.getUnderlyingAUs(ptr,ctx,aus) )
    return -1;

  return getSubHeap(aus);
}

static int joinSubHeaps(int a, int b)
{
  if( -1 == a )
    return b;
  if( -1 == b )
    return a;
  if( a == b )
    return a;
  else
    return -1;
}

int HeapAssignment::getSubHeap(Ptrs &aus) const
{
  int res = -1;
  for(unsigned i=0; i<aus.size(); ++i)
  {
    AU *au = aus[i].au;
    if( au->type == AU_Null )
      continue;

    if( au->type == AU_Unknown )
      return -1;

    if( au->type == AU_Undefined )
      return -1;

    res = joinSubHeaps(res, getSubHeap(au) );
  }

  return res;
}

void HeapAssignment::setSubHeap(const AU *au, int sh)
{
  if( 0 > sh )
    subheaps.erase(au);

  else
    subheaps[au] = sh;
}

void HeapAssignment::setValidFor(const Loop *loop)
{
  success.insert( loop->getHeader() );
}

bool HeapAssignment::isValid() const
{
  return !success.empty();
}

bool HeapAssignment::isValidFor(const Loop *L) const
{
  return success.count( L->getHeader() );
}

bool HeapAssignment::isSimpleCase() const
{
  std::set<const Value *> allocationSites;
  const AUSet &shareds    = getSharedAUs(),
              &locals     = getLocalAUs(),
              &privates   = getPrivateAUs(),
              &ro         = getReadOnlyAUs();

  const ReduxAUSet &reductions = getReductionAUs();

  for(AUSet::const_iterator i=shareds.begin(), e=shareds.end(); i!=e; ++i)
  {
    AU *au = *i;
    allocationSites.insert( au->value );
  }
  for(AUSet::const_iterator i=locals.begin(), e=locals.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( allocationSites.count( au->value ) )
      return false;
  }
  for(AUSet::const_iterator i=locals.begin(), e=locals.end(); i!=e; ++i)
  {
    AU *au = *i;
    allocationSites.insert( au->value );
  }
  for(ReduxAUSet::const_iterator i=reductions.begin(), e=reductions.end(); i!=e; ++i)
  {
    AU *au = i->first;
    if( allocationSites.count( au->value ) )
      return false;
  }
  for(ReduxAUSet::const_iterator i=reductions.begin(), e=reductions.end(); i!=e; ++i)
  {
    AU *au = i->first;
    allocationSites.insert( au->value );
  }
  for(AUSet::const_iterator i=privates.begin(), e=privates.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( allocationSites.count( au->value ) )
      return false;
  }
  for(AUSet::const_iterator i=privates.begin(), e=privates.end(); i!=e; ++i)
  {
    AU *au = *i;
    allocationSites.insert( au->value );
  }
  for(AUSet::const_iterator i=ro.begin(), e=ro.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( allocationSites.count( au->value ) )
      return false;
  }
  /*
  for(AUSet::const_iterator i=ro.begin(), e=ro.end(); i!=e; ++i)
  {
    AU *au = *i;
    allocationSites.insert( au->value );
  }
  */

  return true;
}

HeapAssignment::Type HeapAssignment::classify(const Value *ptr, const Loop *loop, const Read &spresults) const
{
  // Map ptr to AUs.
  Ptrs aus;
  const Ctx *ctx = spresults.getCtx(loop);
  if( ! spresults.getUnderlyingAUs(ptr,ctx,aus) )
    return HeapAssignment::Unclassified;

  return classify(aus);
}

HeapAssignment::Type HeapAssignment::classify(Ptrs &aus) const
{
  Type res = Unclassified;
  for(unsigned i=0; i<aus.size(); ++i)
  {
    AU *au = aus[i].au;
    if( au->type == AU_Null )
      continue;

    if( au->type == AU_Unknown )
      return Unclassified;

    if( au->type == AU_Undefined )
      return Unclassified;

    Type ty = classify(au);

    // first time through loop
    if( res == Unclassified )
      res = ty;

    // later iterations: ensure consistency!
    else if( res != ty )
      return Unclassified;
  }

  return res;
}

HeapAssignment::Type HeapAssignment::classify(AU *au) const
{
  const AUSet &shareds = getSharedAUs();
  if( shareds.count(au) )
    return Shared;

  const AUSet &locals = getLocalAUs();
  if( locals.count(au) )
    return Local;

  const ReduxAUSet &reductions = getReductionAUs();
  if( reductions.count(au) )
    return Redux;

  const AUSet &privates = getPrivateAUs();
  if( privates.count(au) )
    return Private;

  const AUSet &ro = getReadOnlyAUs();
  if( ro.count(au) )
    return ReadOnly;

//  errs() << "AU not classified within loop: " << *au << '\n';
  return Unclassified;
}

HeapAssignment::AUSet &HeapAssignment::getSharedAUs() {  return shareds; }
HeapAssignment::AUSet &HeapAssignment::getLocalAUs() { return locals; }
HeapAssignment::AUSet &HeapAssignment::getPrivateAUs() { return privs; }
HeapAssignment::AUSet &HeapAssignment::getReadOnlyAUs() { return ros; }
HeapAssignment::ReduxAUSet &HeapAssignment::getReductionAUs() { return reduxs; }
HeapAssignment::ReduxDepAUSet &HeapAssignment::getReduxDepAUs() { return reduxdeps; }

const HeapAssignment::AUSet &HeapAssignment::getSharedAUs() const { return shareds; }
const HeapAssignment::AUSet &HeapAssignment::getLocalAUs() const { return locals; }
const HeapAssignment::AUSet &HeapAssignment::getPrivateAUs() const { return privs; }
const HeapAssignment::AUSet &HeapAssignment::getReadOnlyAUs() const { return ros; }
const HeapAssignment::ReduxAUSet &HeapAssignment::getReductionAUs() const { return reduxs; }
const HeapAssignment::ReduxDepAUSet &HeapAssignment::getReduxDepAUs() const { return reduxdeps; }

bool HeapAssignment::compatibleWith(const HeapAssignment &other) const
{
  // For each of my AUs:
  return compatibleWith(ReadOnly, other.getReadOnlyAUs())
  &&     compatibleWith(Shared,   other.getSharedAUs())
  &&     compatibleWith(Redux,    other.getReductionAUs())
  &&     compatibleWith(Local,    other.getLocalAUs())
  &&     compatibleWith(Private,  other.getPrivateAUs());
}

bool HeapAssignment::compatibleWith(Type ty, const AUSet &set) const
{
  assert( ty != Redux );

  for(AUSet::const_iterator i=set.begin(), e=set.end(); i!=e; ++i)
  {
    Type myty = classify(*i);
    if( myty == Unclassified )
      continue;

    if( join(myty, ty) == Unclassified )
      return false;
  }
  return true;
}

bool HeapAssignment::compatibleWith(Type ty, const ReduxAUSet &rset) const
{
  assert( ty == Redux );

  for(ReduxAUSet::const_iterator i=rset.begin(), e=rset.end(); i!=e; ++i)
  {
    Type myty = classify(i->first);
    if( myty == Unclassified )
      continue;

    if( join(myty,ty) == Unclassified )
      return false;

    if( myty == Redux )
    {
      const ReduxAUSet &myrx = getReductionAUs();
      ReduxAUSet::const_iterator j = myrx.find( i->first );
      assert( j != myrx.end() );

      // Ensure they are the same kind of reduction
      if( i->second != j->second )
        return false;
    }
  }
  return true;
}

void HeapAssignment::accumulate(const HeapAssignment &A, Type ty0, const AUSet &Bset)
{
  assert( ty0 != Redux );
  for(AUSet::const_iterator i=Bset.begin(), e=Bset.end(); i!=e; ++i)
  {
    AU *au = *i;
    Type ty = A.classify(au);
    if( ty == Unclassified )
      ty = ty0;
    else
      ty = join(ty, ty0);
    assert( ty != Unclassified );

    switch( ty )
    {
      case ReadOnly:
        ros.insert(au);
        break;
      case Shared:
        shareds.insert(au);
        break;
      case Redux:
        assert( false && "Impossible?!");
        break;
      case Local:
        locals.insert(au);
        break;
      case Private:
        privs.insert(au);
        break;
      default:
        assert( false && "This should not happen");
        break;
    }
  }
}

void HeapAssignment::accumulate(const HeapAssignment &A, Type ty0, const ReduxAUSet &Bset)
{
  assert( ty0 == Redux );
  for(ReduxAUSet::const_iterator i=Bset.begin(), e=Bset.end(); i!=e; ++i)
  {
    AU *au = i->first;
    Type ty = A.classify(au);
    if( ty == Unclassified )
      ty = ty0;
    else
      ty = join(ty, ty0);
    assert( ty != Unclassified );

    assert( ty == Redux );
    ReduxAUSet::const_iterator j = A.reduxs.find(au);
    if( j != A.reduxs.end() )
      assert( j->second == i->second );
    reduxs[ au ] = i->second;
  }
}

HeapAssignment HeapAssignment::operator&(const HeapAssignment &other) const
{
  HeapAssignment asgn;

  asgn.accumulate(*this, ReadOnly, other.getReadOnlyAUs());
  asgn.accumulate(*this, Shared, other.getSharedAUs());
  asgn.accumulate(*this, Redux, other.getReductionAUs());
  asgn.accumulate(*this, Local, other.getLocalAUs());
  asgn.accumulate(*this, Private, other.getPrivateAUs());

  asgn.accumulate(other, ReadOnly, this->getReadOnlyAUs());
  asgn.accumulate(other, Shared, this->getSharedAUs());
  asgn.accumulate(other, Redux, this->getReductionAUs());
  asgn.accumulate(other, Local, this->getLocalAUs());
  asgn.accumulate(other, Private, this->getPrivateAUs());

  asgn.success.insert( this->success.begin(), this->success.end() );
  asgn.success.insert( other.success.begin(), other.success.end() );

  asgn.assignSubHeaps();
  return asgn;
}

void HeapAssignment::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - HeapAssignment::contextRenamedViaClone: " << *changedContext << '\n';

  const ValueToValueMapTy::const_iterator vmap_end = vmap.end();

  LoopSet newLoops;
  for(LoopSet::const_iterator i=success.begin(), e=success.end(); i!=e; ++i)
  {
    const BasicBlock *header = *i;
    newLoops.insert(header);
    ValueToValueMapTy::const_iterator j = vmap.find(header);
    if( j == vmap_end )
      continue;
    const BasicBlock *newHeader = cast< BasicBlock >( &*(j->second) );
    newLoops.insert( newHeader );
  }
  success.insert( newLoops.begin(), newLoops.end() );

  updateAUSet(shareds, amap);
  updateAUSet(locals, amap);
  updateAUSet(privs, amap);
  updateAUSet(ros, amap);
  updateAUSet(reduxs, amap);
}

void HeapAssignment::updateAUSet(AUSet &aus, const AuToAuMap &amap)
{
  const AuToAuMap::const_iterator amap_end = amap.end();

  AUSet newSet;
  for(AUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *old = *i;
    AuToAuMap::const_iterator j = amap.find( old );
    if( j == amap_end )
      newSet.insert( old );
    else
      newSet.insert( j->second );
  }

  aus.swap(newSet);
}

void HeapAssignment::updateAUSet(ReduxAUSet &aus, const AuToAuMap &amap)
{
  const AuToAuMap::const_iterator amap_end = amap.end();

  ReduxAUSet newSet;
  for(ReduxAUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *old = i->first;
    Reduction::Type ty = i->second;

    AuToAuMap::const_iterator j = amap.find( old );
    if( j == amap_end )
      newSet.insert( ReduxAUSet::value_type(old,ty) );
    else
      newSet.insert( ReduxAUSet::value_type(j->second, ty) );
  }

  aus.swap(newSet);
}

void Classify::contextRenamedViaClone(
  const Ctx *cc,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - Classify::contextRenamedViaClone: " << *cc << '\n';
  const ValueToValueMapTy::const_iterator vend = vmap.end();

  Loop2Assignments new_asgns;
  for(Loop2Assignments::iterator i=assignments.begin(), e=assignments.end(); i!=e; ++i)
  {
    const BasicBlock *header = i->first;
    i->second.contextRenamedViaClone(cc,vmap,cmap,amap);

    const ValueToValueMapTy::const_iterator j = vmap.find(header);
    if( j != vend )
    {
      const BasicBlock *new_header = cast< BasicBlock >( &*( j->second ) );
      new_asgns[ new_header ] = i->second;
    }
  }

  assignments.insert( new_asgns.begin(), new_asgns.end() );
}

bool compatible(const HeapAssignment &A, const HeapAssignment &B)
{
  return A.compatibleWith(B) && B.compatibleWith(A);
}

}
}
