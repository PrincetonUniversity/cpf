#include "llvm/IR/IntrinsicInst.h"

#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "liberty/Utilities/ModuleLoops.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

void ProfilePerformanceEstimator::getAnalysisUsage(AnalysisUsage &au) const
{
  //au.addRequired< ProfileInfo >();
  au.addRequired< BlockFrequencyInfoWrapperPass >();
  au.addRequired< ModuleLoops >();
  au.addRequired< LoopProfLoad >();
  au.setPreservesAll();
}

bool ProfilePerformanceEstimator::runOnModule(Module &mod)
{
  return false;
}

unsigned ProfilePerformanceEstimator::instruction_type_weight(const Instruction *inst)
{
  if( isa<PHINode>(inst) )
    return 1;
  else if( isa<BranchInst>(inst) )
    return 1;
  else if( isa<SwitchInst>(inst) )
    return 1;

  if( const IntrinsicInst *intrin = dyn_cast< IntrinsicInst >(inst) )
  {
    // Intrinsics which do not do anything.
    if( intrin->getIntrinsicID() == Intrinsic::lifetime_start
    ||  intrin->getIntrinsicID() == Intrinsic::lifetime_end
    ||  intrin->getIntrinsicID() == Intrinsic::invariant_start
    ||  intrin->getIntrinsicID() == Intrinsic::invariant_end )
      return 1;
  }

  if( inst->mayReadOrWriteMemory() )
    return 200;
  else
    return 100;
}

unsigned long ProfilePerformanceEstimator::relative_weight(const Instruction *inst)
{
  // edge-count profile results
  //ProfileInfo &pi = getAnalysis< ProfileInfo >();

  const BasicBlock *bb = inst->getParent();
  const Function *fcn = bb->getParent();

  // Evil, but okay because it won't modify the IR
  Function *non_const_fcn = const_cast<Function*>(fcn);
  BlockFrequencyInfo &bfi = getAnalysis< BlockFrequencyInfoWrapperPass >(*non_const_fcn).getBFI();

  //sot
  //const double fcnt = pi.getExecutionCount(fcn);
  //getEntryCount returns -1 if no value in LLVM 7.0
  // in LLVM 5.0 it returns llvm::Optional<long unsigned int>
  auto fcnt = fcn->getEntryCount();
  if ((fcnt.hasValue() && fcnt.getValue() < 1) || !fcnt.hasValue())
  {
    // Function never executed or no profile info available, so we don't know
    // the relative weights of the blocks inside.  We will assign the same
    // relative weight to all blocks in this function.

    return 100 * instruction_type_weight(inst);
  }
  else
  {
    //sot
    //const double bbcnt = pi.getExecutionCount(bb);
    if (!bfi.getBlockProfileCount(bb).hasValue()) {
      errs() << "No profile count for BB " << bb->getName() << "\n";
      return 100 * instruction_type_weight(inst);
    }
    const double bbcnt = bfi.getBlockProfileCount(bb).getValue();
    const unsigned long bbicnt = (unsigned) (100 * bbcnt);

    // errs() << "bbcnt, bbicnt: " << bbcnt << " " << bbicnt << "\n";
    return bbicnt * instruction_type_weight(inst);
  }
}

double ProfilePerformanceEstimator::estimate_weight(const Instruction *inst)
{
  //EPLoad& epload = getAnalysis< EPLoad >();
  //return epload.getCost(inst);

  if( isa<CallInst>(inst) || isa<InvokeInst>(inst) )
  {
    LoopProfLoad &lprof = getAnalysis< LoopProfLoad >();
    // errs() << "Assume call or invoke\n";
    return lprof.getCallSiteTime(inst);
  }

  const BasicBlock *bb = inst->getParent();
  const Function *fcn = bb->getParent();
  visit(fcn);

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  LoopInfo &loops = mloops.getAnalysis_LoopInfo(fcn);
  const Loop *loop = loops.getLoopFor(bb);

  const TimeAndWeight &tw = ctx2timeAndWeight[ Context(fcn,loop) ];
  const unsigned long local_weight = tw.first;
  const unsigned long sum_local_relative = tw.second;
  if( 0 == local_weight || 0 == sum_local_relative )
    return 0;

  const unsigned long relative = relative_weight(inst);

  // errs() << "local, relative, sum: " << local_weight << " " << relative << " " << sum_local_relative << "\n";
  return local_weight * (double)relative / (double)sum_local_relative;
}

double ProfilePerformanceEstimator::estimate_parallelization_weight(const Instruction *inst, const Loop* target_loop)
{
  // parallelization weight:
  // (# of target loop iteration that 'inst' has been executed) / (# of target loop iteration)

  const BasicBlock* target_header = target_loop->getHeader();

  const BasicBlock* bb = inst->getParent();
  const Function*   fcn = bb->getParent();

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  LoopInfo    &loops = mloops.getAnalysis_LoopInfo(fcn);
  //sot
  //ProfileInfo &pi = getAnalysis< ProfileInfo >();
  // Evil, but okay because it won't modify the IR
  Function *non_const_fcn = const_cast<Function*>(fcn);
  BlockFrequencyInfo &bfi = getAnalysis< BlockFrequencyInfoWrapperPass >(*non_const_fcn).getBFI();

  const Loop*       loop = loops.getLoopFor(bb);
  const BasicBlock* header = loop->getHeader();

  //sot
  //if ( pi.getExecutionCount(bb) == ProfileInfo::MissingValue ) return 0.0;
  //if ( pi.getExecutionCount(header) == ProfileInfo::MissingValue ) return 0.0;
  //if ( pi.getExecutionCount(header) == 0 ) return 0.0;

  if (!fcn->getEntryCount().hasValue()) return 0.0;
  if (!bfi.getBlockProfileCount(bb).hasValue()) return 0.0;
  if (!bfi.getBlockProfileCount(header).hasValue()) return 0.0;
  const double bbcnt = bfi.getBlockProfileCount(bb).getValue();
  const double headercnt = bfi.getBlockProfileCount(header).getValue();
  if (headercnt == 0) return 0.0;

  //double w = pi.getExecutionCount(bb) / pi.getExecutionCount(header);
  double w = (bbcnt * 1.0) / headercnt;

  while (header != target_header)
  {
    const BasicBlock* preheader = loop->getLoopPreheader();

    loop = loops.getLoopFor(preheader);
    header = loop->getHeader();

    //sot
    //if ( pi.getExecutionCount(preheader) == ProfileInfo::MissingValue ) return 0.0;
    //if ( pi.getExecutionCount(header) == ProfileInfo::MissingValue ) return 0.0;
    //if ( pi.getExecutionCount(header) == 0 ) return 0.0;

    //double r = pi.getExecutionCount(preheader) / pi.getExecutionCount(header);

    if (!bfi.getBlockProfileCount(preheader).hasValue()) return 0.0;
    if (!bfi.getBlockProfileCount(header).hasValue()) return 0.0;
    const double preheadercnt = bfi.getBlockProfileCount(preheader).getValue();
    const double headercnt = bfi.getBlockProfileCount(header).getValue();
    if (headercnt == 0) return 0.0;

    double r = (preheadercnt * 1.0) / headercnt;

    w *= r;
  }

  return w;
}

void ProfilePerformanceEstimator::visit(const Function *fcn)
{
  // Analyze each function at most once.
  if( ctx2timeAndWeight.count( Context(fcn,0) ) )
    return;

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  LoopInfo &loops = mloops.getAnalysis_LoopInfo(fcn);

  visit(fcn,0,
    // subloops, type LoopInfo::iterator
    loops.begin(),loops.end(),
    // member basic blocks, type Function::const_iterator
    fcn->begin(),fcn->end());
}

template <class SubLoopIter, class MemberIter>
void ProfilePerformanceEstimator::visit(const Function *fcn, const Loop *loop, const SubLoopIter &subloop_begin, const SubLoopIter &subloop_end, const MemberIter &members_begin, const MemberIter &members_end)
{
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  LoopInfo &loops = mloops.getAnalysis_LoopInfo(fcn);
  LoopProfLoad &lprof = getAnalysis< LoopProfLoad >();

  const unsigned long outside_weight = loop ? lprof.getLoopTime(loop) : lprof.getFunctionTime(fcn);

  // Foreach sub-loop
  unsigned long sum_nested_loops = 0;
  for(SubLoopIter i=subloop_begin; i!=subloop_end; ++i)
  {
    // Recur on sub-contexts
    const Loop *subloop = MakePointer( *i );
    visit(fcn,subloop,
      // subloops, type Loop::iterator
      subloop->begin(), subloop->end(),
      // members, type Loop::block_iterator
      subloop->block_begin(), subloop->block_end());

    sum_nested_loops += lprof.getLoopTime(subloop);
  }

  // Foreach basic block not in a sub-context
  unsigned long sum_nested_callsites = 0;
  unsigned long sum_relative_weights_of_locals = 0;
  for(MemberIter i=members_begin; i!=members_end; ++i)
  {
    const BasicBlock *bb = MakePointer( *i );
    if( loops.getLoopFor(bb) != loop )
      continue;

    // Foreach instruction local to this context.
    for(BasicBlock::const_iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      const Instruction *inst = &*j;

      // Do we have a time measurement for this?
      if( isa<CallInst>(inst) || isa<InvokeInst>(inst) )
        sum_nested_callsites += lprof.getCallSiteTime(inst);

      else
        sum_relative_weights_of_locals += relative_weight(inst);
    }
  }

  const unsigned long local_time = outside_weight - sum_nested_loops - sum_nested_callsites;

  // if (loop)
  //   errs() << "Fcn " << fcn->getName() << " :: " << loop->getHeader()->getName() << " local_time " << local_time << ", sum_rel_wt_locals " << sum_relative_weights_of_locals << "\n";
  // else
  //   errs() << "Fcn " << fcn->getName() << " local_time " << local_time << ", sum_rel_wt_locals " << sum_relative_weights_of_locals << "\n";
  ctx2timeAndWeight[ Context(fcn,loop) ] = TimeAndWeight(local_time, sum_relative_weights_of_locals);
}

void ProfilePerformanceEstimator::reset()
{
  // reset our cache
  ctx2timeAndWeight.clear();
}

char ProfilePerformanceEstimator::ID = 0;
static RegisterPass< ProfilePerformanceEstimator > ppe("profile-performance-estimator", "Profile-guided performance estimator");

}
}
