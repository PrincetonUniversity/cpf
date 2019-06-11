#define DEBUG_TYPE "targets"

#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/LoopProf/Targets.h"

#include <list>

namespace liberty
{

static cl::opt<bool> TargetAllLoops(
  "target-all", cl::init(false), cl::Hidden,
  cl::desc("Target EVERY loop in the program"));

static cl::opt<std::string> ExplicitTargetFcn(
  "target-fcn", cl::init(""), cl::NotHidden,
  cl::desc("Explicit Target Function"));

static cl::opt<std::string> ExplicitTargetLoop(
  "target-loop", cl::init(""), cl::NotHidden,
  cl::desc("Explicit Target Loop"));

static cl::list<std::string> ExplicitTargets(
  "target-list", cl::NotHidden, cl::CommaSeparated,
     cl::desc("Explicit Target List: f1,bb1,f2,bb2,..."));

static cl::opt<unsigned> MinExecTimePercent(
  "target-min-exec-percent", cl::init(5), cl::NotHidden,
  cl::desc("Target loops whose execution is at least N% of the total"));

static cl::opt<unsigned> MinIterationsPerInvoc(
  "target-min-iters", cl::init(8), cl::NotHidden,
  cl::desc("Target loops which iterate at least N times per invocation on average"));

Loop *header_to_loop_mapping_iterator::operator*() const
{
  BasicBlock *header = *i;
  Function *fcn = header->getParent();
  DEBUG(errs() << "header is: " << *header);
  DEBUG(errs() << "function name is: " << fcn->getName() << "\n");
  LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);
  Loop *loop = li.getLoopFor(header);
  assert( loop && "Specified header not within a loop???");
  DEBUG(errs() << "Loop->getHeader is: " << *(loop->getHeader()));
  assert( loop->getHeader() == header && "The specified header is not the loop header???");
  return loop;
}

bool header_to_loop_mapping_iterator::operator==(const header_to_loop_mapping_iterator &other) const
{
  return i == other.i;
}

bool header_to_loop_mapping_iterator::operator!=(const header_to_loop_mapping_iterator &other) const
{
  return i != other.i;
}

const header_to_loop_mapping_iterator &header_to_loop_mapping_iterator::operator++()
{
  ++i;
  return *this;
}


//bool Targets::expectsManyIterations(const Loop *loop, const std::string &fname, const std::string &hname)
bool Targets::expectsManyIterations(const Loop *loop)
{
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  //ProfileInfo &pi = getAnalysis< ProfileInfo >();
  //if( pi.getExecutionCount(fcn) == ProfileInfo::MissingValue )
  //  return true;
  if ( !fcn->getEntryCount().hasValue() )
    return true;

  BlockFrequencyInfo &bfi = getAnalysis< BlockFrequencyInfoWrapperPass >(*fcn).getBFI();
  BranchProbabilityInfo &bpi = getAnalysis< BranchProbabilityInfoWrapperPass >(*fcn).getBPI();

  double totalInvocations = 0;
  double totalBackedgeCount = 0;
  for(pred_iterator i=pred_begin(header), e=pred_end(header); i!=e; ++i)
  {
    BasicBlock *pred = *i;

    //const double edgeCount = pi.getEdgeWeight( ProfileInfo::Edge(pred,header) );
    //if( edgeCount == ProfileInfo::MissingValue )
    //  continue;
    // auto prob = bpi.getEdgeProbability(pred, header);
    // const double rate = prob.getNumerator() / (double) prob.getDenominator();
    if ( !bfi.getBlockProfileCount(pred).hasValue() )
      continue;
    const double pred_cnt = bfi.getBlockProfileCount(pred).getValue();
    // const double edgeCount = ((double) bfi.getBlockFreq(pred).getFrequency() * fcn->getEntryCount().getValue()) / bfi.getEntryFreq();
    //const double edgeCount = bpi.getEdgeProbability(pred, header).scale(bfi.getBlockProfileCount(pred).getValue());
    auto prob = bpi.getEdgeProbability(pred, header);
    const double edgeCount = pred_cnt * (double(prob.getNumerator()) / double(prob.getDenominator()));

    //or I could have used the numerator but the above is safer in case the numerator and denominator get scaled
    //uint32_t edgeCount = prob.getNumerator();

    if( loop->contains(pred) )
      totalBackedgeCount += edgeCount;
    else
      totalInvocations += edgeCount;
  }

  if( totalInvocations < 1 )
    return false;

  const double totalIterations = totalInvocations + totalBackedgeCount;

  //DEBUG(errs() << "Loop edge profiling " << fname << "::" << hname << "\t totalIterations is : "  << totalIterations <<  ", totalInvocations is : " << totalInvocations <<  "\n");
  return (totalIterations > MinIterationsPerInvoc * totalInvocations);
}

void Targets::addLoopByName(Module &mod, const std::string &fname, const std::string &hname, unsigned long wt, bool minIterCheck)
{
  Function *fcn = mod.getFunction( fname );
  assert( fcn && "The specified function does not exist");

  // already got mloops
  //ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  LoopInfo &li = mloops->getAnalysis_LoopInfo(fcn);

  if( hname == "" )
  {
    // Add all loops within this function.
    // List loops before their subloops
    std::list<Loop*> fringe( li.begin(), li.end() );
    while( !fringe.empty() )
    {
      Loop *l = fringe.front();
      fringe.pop_front();

      //if( !minIterCheck || expectsManyIterations(l, fname, hname) )
      if( !minIterCheck || expectsManyIterations(l) )
        Loops.push_back(l->getHeader());

      fringe.insert( fringe.end(),
        l->begin(), l->end() );
    }
  }
  else
  {
    // Find this basic block.
    BasicBlock *bb = 0;
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
      if( i->getName() == hname )
      {
        bb = &*i;
        break;
      }
    assert( bb  && "The specified block does not exist in that function" );

    Loop *loop = li.getLoopFor(bb);
    assert( loop && "The specified block is not within a loop");
    assert( loop->getHeader() == bb && "The specified block is not a loop header");

    if( minIterCheck )
      //if( !expectsManyIterations(loop, fname, hname) )
      if( !expectsManyIterations(loop) )
      {
        DEBUG(errs() << "Ignoring loop " << fname << "::" << hname << "\t(wt "<< wt <<") because too few iters/invoc\n");
        return;
      }

    Loops.push_back(loop->getHeader());
  }
}

struct SortLoops
{
  SortLoops( LoopProfLoad &lpl ) : load(lpl) {}

  bool operator()(const BasicBlock * const & a, const BasicBlock * const & b)
  {
    return load.getLoopTime(a) > load.getLoopTime(b);
  }
private:
  LoopProfLoad &load;
};

bool Targets::runOnModule(Module &mod)
{
  LoopProfLoad &load = getAnalysis< LoopProfLoad >();

  mloops = &getAnalysis< ModuleLoops >();

  // Favor explicit over implicit.
  if( TargetAllLoops )
  {
    for(Module::iterator i=mod.begin(), e=mod.end(); i!=e; ++i)
    {
      Function *fcn = &*i;
      if( fcn->isDeclaration() )
        continue;
      addLoopByName( mod, fcn->getName(), "", 0 );
    }
  }

  else if( !ExplicitTargets.empty() )
  {
    for(unsigned i=0; i<ExplicitTargets.size(); i+=2)
    {
      if( i+1 < ExplicitTargets.size() )
        addLoopByName( mod, ExplicitTargets[i], ExplicitTargets[i+1], 0 );
      else
        addLoopByName( mod, ExplicitTargets[i], "", 0 );
    }
  }

  else if( ExplicitTargetFcn != "" )
  {
    addLoopByName( mod, ExplicitTargetFcn, ExplicitTargetLoop, 0 );
  }

  else
  {
    assert( load.isValid() && "You must either specify an explicit target, or provide a loop profile");

    // Add all loops whose execution time is at least 10% of program runtime.
    // and which iterate at least N times.
    double min = load.getTotTime() * (double)MinExecTimePercent / 100;
    for(LoopProfLoad::iterator i=load.begin(), e=load.end(); i!=e; ++i)
    {
      const std::string &name = i->first;

      // Skip callsites
      if( name.find("!callsite") == 0 )
        continue;

      // Skip functions
      const size_t split = name.find(':');
      if( split == std::string::npos )
        continue;

      std::string fname = name.substr(0, split);
      std::string hname = name.substr(split+1);

      if( i->second < min )
      {
        DEBUG(errs() << "Ignoring loop " << fname << "::" << hname << "\tTime: " << i->second << " because too little weight\n");
        continue;
      }

      addLoopByName( mod, fname, hname, i->second, true );
    }
  }

  // Sort loops by execution weight, descending, if available.
  if( Loops.size() > 1 && load.isValid() )
  {
    SortLoops ltne(load);
    std::stable_sort( Loops.begin(), Loops.end(), ltne );
  }

  errs() << "Focus on these loops (in this order):\n";
  for(header_iterator i=begin(), e=end(); i!=e; ++i)
  {
    BasicBlock *header = *i;
    Function *fcn = header->getParent();

    char percent[10];
    const unsigned long loop_time = load.getLoopTime(header);
    snprintf(percent,10, "%.1f", 100.0 * loop_time / load.getTotTime());

    errs() << " - " << fcn->getName() << " :: " << header->getName();
    Instruction *term = header->getTerminator();
    if (term)
      liberty::printInstDebugInfo(term);
    errs() << "\tTime " << loop_time << " / " << load.getTotTime()
           << " Coverage: " << percent << "%\n";
  }

  return false;
}

char Targets::ID = 0;
static RegisterPass< Targets > rp("targets", "Find target loops");

}
