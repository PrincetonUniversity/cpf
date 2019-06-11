#define DEBUG_TYPE "ctrlspec"

#include "liberty/Analysis/CallsiteSearch.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ControlSpecIterators.h"
#include "liberty/Analysis/Introspection.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/Timer.h"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"


namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


STATISTIC(numSpecEdges,        "Speculatively Dead Edges");
STATISTIC(numTotalBlocks,      "Total basic blocks visited");
STATISTIC(numSpecBlocks,       "Speculatively Dead Blocks");

void ProfileGuidedControlSpeculator::reset()
{
  ControlSpeculation::reset();

  loops.clear();
//  visited.clear();
//  deadEdges.clear();
//  deadBlocks.clear();
}


void ProfileGuidedControlSpeculator::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
  for(PerLoopData::const_iterator i=loops.begin(), e=loops.end(); i!=e; ++i)
  {
    const BasicBlock *header = i->first;


  //  errs() << "  . . - ProfileGuidedControlSpeculator::contextRenamedViaClone: " << *changedContext << '\n';
    CtrlEdges &deadEdges = loops[ header ].deadEdges;

    if( changedContext->type != Ctx_Fcn )
      return;
    const Function *fcn = changedContext->getFcn();

    visit(fcn);

    const ValueToValueMapTy::const_iterator j = vmap.find(fcn), vmap_end = vmap.end();
    assert( j != vmap_end );

    const Function *clone = cast<Function>( &*(j->second) );
    if( loops[header].visited.count(clone) )
      return;

    CtrlEdges newDeadEdges;
    for(CtrlEdges::const_iterator i=deadEdges.begin(), e=deadEdges.end(); i!=e; ++i)
    {
      const TerminatorInst *term = i->first;
      ValueToValueMapTy::const_iterator j = vmap.find(term);
      if( j == vmap_end )
        continue;

      const TerminatorInst *newTerm = cast< TerminatorInst >( &*(j->second) );
      newDeadEdges[ newTerm ] = i->second;

  //    errs() << "  - t  " << term->getParent()->getParent()->getName() << "::"
  //           << term->getParent()->getName() << ": " << *term << " #" << i->second << '\n'
  //           << newTerm->getParent()->getName() << ": " << *newTerm << " #" << i->second << '\n';

    }
    deadEdges.insert( newDeadEdges.begin(), newDeadEdges.end() );

    BlockSet &deadBlocks = loops[ header ].deadBlocks;
    BlockSet newDeadBlocks;
    for(BlockSet::const_iterator i=deadBlocks.begin(), e=deadBlocks.end(); i!=e; ++i)
    {
      const BasicBlock *bb = *i;
      ValueToValueMapTy::const_iterator j = vmap.find(bb);
      if( j == vmap_end )
        continue;

      const BasicBlock *newBB = cast< BasicBlock >( &*(j->second) );
      newDeadBlocks.insert( newBB );

  //    errs() << "  - b  " << bb->getParent()->getName() << "::" << bb->getName() << '\n'
  //           << "    => " << newBB->getParent()->getName() << "::" << newBB->getName() << '\n';
    }
    deadBlocks.insert( newDeadBlocks.begin(), newDeadBlocks.end() );

    loops[header].visited.insert( clone );
  }
}


char ProfileGuidedControlSpeculator::ID=0;
static RegisterPass<ProfileGuidedControlSpeculator> x("ctrl-spec", "Control Speculation Manager", false, false);


void ProfileGuidedControlSpeculator::getAnalysisUsage(AnalysisUsage &au) const
{
  //au.addRequired< DataLayout >();
  au.addRequired< ModuleLoops >();
//  au.addRequired< LoopAA >();
  au.addRequired< BlockFrequencyInfoWrapperPass >();
  au.addRequired< BranchProbabilityInfoWrapperPass >();
  au.addRequired< Targets >();
  au.setPreservesAll();
}

bool ProfileGuidedControlSpeculator::dominatesTargetHeader(const BasicBlock* bb)
{
  const Function* fcn = bb->getParent();
  auto targetHeader = getLoopHeaderOfInterest();
  if (fcn != targetHeader->getParent())
    return false;
  DominatorTree&  dt = this->mloops->getAnalysis_DominatorTree( fcn );
  if ( dt.dominates( bb, targetHeader ) ) {
    DEBUG(errs() << "bb " << bb->getName() << " dominates target header "
                 << targetHeader->getName() << "\n");
    return true;
  }
  return false;
}

void ProfileGuidedControlSpeculator::visit(const Function *fcn)
{
  // Analyze every function AT MOST once.
  if( loops[getLoopHeaderOfInterest()].visited.count(fcn) )
    return;
  loops[getLoopHeaderOfInterest()].visited.insert(fcn);

  DEBUG(errs() << "CtrlSpec: visit( " << fcn->getName() << " )\n");

  // Evil, but okay because it won't modify the IR
  Function *non_const_fcn = const_cast<Function*>(fcn);

  //ProfileInfo &pi = getAnalysis< ProfileInfo >();
  BlockFrequencyInfo &bfi = getAnalysis< BlockFrequencyInfoWrapperPass >(*non_const_fcn).getBFI();
  BranchProbabilityInfo &bpi = getAnalysis< BranchProbabilityInfoWrapperPass >(*non_const_fcn).getBPI();

  // How confident must we be before speculating?
  //const double MinSamples = 10.0;
  const double MinSamples = 5.0;
  const double MaxMisspec = 0.00001; // 0.001%
  const double MaxMisspecLoopExit = 0.0;

  // speculate loop exit (infinite loop) if trip count is at least 10.
  // this min trip count should be close to the loopProf threshold for hot loops
  const double MaxMisspecTargetLoopExit = 0.1; // 10%

  // Decline to comment on functions that were never invoked.
  // (we speculate that the function's callsites never run,
  // but don't speculate anything inside the function...)
  //const double fcnt = pi.getExecutionCount( fcn );
  //sot
  if (!fcn->getEntryCount().hasValue())
  {
    DEBUG(errs() << "CtrlSpec: function does not have profile data avaiable\n");

    // sot
    // In LLVM 5.0 getEntryCount will return none for zero counts. Thus no
    // profile data vs never invoked functions are indistinguisable.
    // Re-read metadata here to distuinguish the two cases.
    MDNode *MD = fcn->getMetadata(LLVMContext::MD_prof);
    if (MD && MD->getOperand(0))
      if (MDString *MDS = dyn_cast<MDString>(MD->getOperand(0)))
        if (MDS->getString().equals("function_entry_count")) {
          ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(1));
          uint64_t Count = CI->getValue().getZExtValue();
          if (Count == 0) {
            // When a function is never invoked make all its basic blocks
            // speculatively dead. This fix is required for privateer, where no
            // profile data is collected for all speculatively dead code.
            BlockSet &deadBlocks = loops[getLoopHeaderOfInterest()].deadBlocks;
            for (Function::const_iterator i = fcn->begin(), e = fcn->end();
                 i != e; ++i) {
              const BasicBlock *bb = &*i;

              DEBUG(errs() << "CtrlSpec " << fcn->getName() << ": block "
                           << bb->getName() << " is speculatively dead.\n");

              deadBlocks.insert(bb);
              ++numSpecBlocks;
            }
          }
        }
    // sot. end

    return;
  }

  uint64_t fcnt = fcn->getEntryCount().getValue();
  if( fcnt == 0 ) // not possible in LLVM 5.0
  {
    DEBUG(errs() << "CtrlSpec: function never executed\n");

    // sot
    // When a function is never invoked make all its basic blocks speculatively
    // dead. This fix is required for privateer, where no profile data is
    // collected for all speculatively dead code.
    BlockSet &deadBlocks = loops[getLoopHeaderOfInterest()].deadBlocks;
    for (Function::const_iterator i = fcn->begin(), e = fcn->end(); i != e;
         ++i) {
      const BasicBlock *bb = &*i;

      DEBUG(errs() << "CtrlSpec " << fcn->getName() << ": block "
                   << bb->getName() << " is speculatively dead.\n");

      deadBlocks.insert(bb);
      ++numSpecBlocks;
    }
    // sot. end

    return;
  }


  LoopInfo &li = mloops->getAnalysis_LoopInfo(fcn);

  // For each conditional branch in this function which executes at least once
  CtrlEdges &deadEdges = loops[ getLoopHeaderOfInterest() ].deadEdges;
  for(Function::const_iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
  {
    const BasicBlock *pred = &*i;

    const TerminatorInst *term = pred->getTerminator();
    const unsigned N = term->getNumSuccessors();
    if( N < 2 )
      continue;

    //const double pred_cnt = pi.getExecutionCount(pred);
    double pred_cnt;
    if ( bfi.getBlockProfileCount(pred).hasValue() )
      pred_cnt = bfi.getBlockProfileCount(pred).getValue();
    else
      pred_cnt = -1;

    // Only HIGH CONFIDENCE speculation
    if(  pred_cnt < MinSamples ) // note: ProfileInfo::MissingValue < 0 < MinSamples
    {
      DEBUG(
        errs() << "Skipping branch at "
               << fcn->getName() << " :: " << pred->getName()
               << " because too few samples.\n");
      continue;
    }

    // For each control flow edge sourced by this branch
    for(unsigned sn=0; sn<N; ++sn)
    {
      const BasicBlock *succ = term->getSuccessor(sn);
      //const double rate = pi.getEdgeWeight( ProfileInfo::Edge(pred,succ) );
      auto prob = bpi.getEdgeProbability(pred, sn);
      // if rate should have been 1, scaling sometimes will make it 0. Silly semantics of PGO.
      //const double rate = prob.scale(pred_cnt);
      const double rate = pred_cnt * (double(prob.getNumerator()) / double(prob.getDenominator()));

      //this will never be less than zero. getEdgeProbability does not inform if weights unknown
      // but it is assumed that if function or the source block has count then the edge will have as well
      //if( rate < 0.0 )
      if( prob.isUnknown() )
      {
        DEBUG(
          errs() << "Will not speculate: edge weight unknown at "
                 << fcn->getName() << " :: " << pred->getName() << "\n");
        continue;
      }

      if ( dominatesTargetHeader( succ ) )
      {
        DEBUG(
          errs() << "Will not speculate: successor dominates target header for edge "
                 << pred->getName() << "->" << succ->getName() << "\n");
        continue;
      }

      // Does this control-flow edge exit a subloop of our loop of interest?
      Loop *lpred = li.getLoopFor(pred);
      if( (lpred && ! lpred->contains(succ))                     // edge exits loop 'lpred'
      &&   lpred->getHeader() != getLoopHeaderOfInterest() ) // and not our loop of interest.
      {
        // This control-flow edge exits a loop, but that
        // loop is NOT our loop of interest.
        // This case is special, because we DO NOT want to
        // speculate that this loop is infinite UNDER ANY CIRCUMSTANCES.
        // If we did that, the loop-of-interest would misspeculate
        // during every iteration!

        // sot: Even after running loop-simplify some loops are not in canonical
        // form and they do not have dedicated exits. Thus, getUniqueExitBlock
        // will lead to an assertion error. Instead check if the loop
        // hasDedicatedExits. If not avoid this loop. TODO: make sure that
        // ignoring loops not in canonical is the right approach here. Maybe we
        // should still check the misspeculation rate and still consider these
        // non-canonical exits

        if (!lpred->hasDedicatedExits())
        {
          DEBUG(
            errs() << "Loop has not dedicated exits (not in canonical form). Avoid speculating this exit edge. Loop at "
                   << fcn->getName() << " :: " << pred->getName() << "\n");
          continue;
        }

        if( lpred->getUniqueExitBlock() == pred )
        {
          DEBUG(
            errs() << "Will not speculate the unique exit of a loop at "
                   << fcn->getName() << " :: " << pred->getName() << "\n");
          continue;
        }

        if( rate > pred_cnt * MaxMisspecLoopExit )
        {
          DEBUG(
            errs() << "Loop exit branch is too frequent (> " << MaxMisspecLoopExit << ") at "
                   << fcn->getName() << " :: " << pred->getName()
                   << ", " << (unsigned)rate << "/" << (unsigned)pred_cnt << "\n");
          continue;
        }
      }
      else
      {
        // This case applies to normal biased branches,
        // as well as to branches which exit the loop
        // of interest.

        bool loopOfInterestExit =
            lpred && !lpred->contains(succ) &&
            lpred->getHeader() == getLoopHeaderOfInterest();

        if (loopOfInterestExit && rate > pred_cnt * MaxMisspecTargetLoopExit) {
          errs() << "Target loop exit " << pred->getName() << "->"
                 << succ->getName() << " cannot be speculated\n";
          continue;
        }

        // Normal biasing threshhold for all branches
        // which are not loop exits.
        if (!loopOfInterestExit && rate > pred_cnt * MaxMisspec) {
          DEBUG(errs() << pred->getName() << "->" << succ->getName() << ", "
                       << (unsigned)rate << " > " << (unsigned)pred_cnt << " * "
                       << format("%f", MaxMisspec) << "\n");
          continue;
        }
      }

      DEBUG(errs() << "CtrlSpec " << fcn->getName()
        << ": speculating that " << pred->getName()
        << " never branches to " << succ->getName()
        << " [observed rate " << (unsigned)rate
        << " over " << (unsigned)pred_cnt << " samples]\n");

      if( ! deadEdges.count(term) )
        deadEdges[term].resize(N);

      deadEdges[term].set(sn);
      ++numSpecEdges;
    }
  }

  // Next, determine which blocks are still reachable
  // without misspeculating.
  std::vector<const BasicBlock*> fringe;
  std::set<const BasicBlock*> reachable;
  fringe.push_back( &fcn->getEntryBlock() );
  while( !fringe.empty() )
  {
    const BasicBlock *bb = fringe.back();
    fringe.pop_back();

    if( reachable.count(bb) )
      continue;
    reachable.insert(bb);

    const TerminatorInst *term = bb->getTerminator();
    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      // Skip speculated edges
      if( isSpeculativelyDead(term,sn) )
        continue;

      // Add successor to fringe.
      fringe.push_back( term->getSuccessor(sn) );
    }
  }

  // Finally, record those blocks which are NOT reachable anymore.
  BlockSet &deadBlocks = loops[ getLoopHeaderOfInterest() ].deadBlocks;
  for(Function::const_iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
  {
    const BasicBlock *bb = &*i;
    ++numTotalBlocks;

    if( ! reachable.count(bb) )
    {
      DEBUG(errs() << "CtrlSpec " << fcn->getName()
        << ": block " << bb->getName() << " is speculatively dead.\n");

      deadBlocks.insert(bb);
      ++numSpecBlocks;
    }
  }
}

bool ProfileGuidedControlSpeculator::isSpeculativelyDead(const TerminatorInst *t, unsigned sn)
{
  const BasicBlock *bb = t->getParent();
  const Function *fcn = bb->getParent();
  visit(fcn);

  const CtrlEdges &deadEdges = loops[ getLoopHeaderOfInterest() ].deadEdges;
  CtrlEdges::const_iterator i = deadEdges.find(t);
  if( i == deadEdges.end() )
    return false;

  return i->second.test(sn);
}

bool ProfileGuidedControlSpeculator::isSpeculativelyDead(const BasicBlock *bb)
{
  const Function *fcn = bb->getParent();
  visit(fcn);

  BlockSet &deadBlocks = loops[ getLoopHeaderOfInterest() ].deadBlocks;
  return deadBlocks.count(bb);
}

void  ProfileGuidedControlSpeculator::dot_block_label(const BasicBlock *bb, raw_ostream &fout) // const  //need to remove due to non_const casting of Function*
{
  fout << bb->getName();

  // Evil, but okay because it won't modify the IR
  Function *non_const_fcn = const_cast<Function*>(bb->getParent());

  //ProfileInfo &pi = getAnalysis< ProfileInfo >();
  BlockFrequencyInfo &bfi = getAnalysis< BlockFrequencyInfoWrapperPass >(*non_const_fcn).getBFI();
  //const double pred_cnt = pi.getExecutionCount(bb);
  if ( !bfi.getBlockProfileCount(bb).hasValue() )
    fout << "\\n(unknown)";
  else
  {
    const double pred_cnt = bfi.getBlockProfileCount(bb).getValue();
    fout << "\\n(" << pred_cnt << ')';
  }
}

void  ProfileGuidedControlSpeculator::dot_edge_label(const TerminatorInst *term, unsigned sn, raw_ostream &fout)
{
  // Evil, but okay because it won't modify the IR
  Function *non_const_fcn = const_cast<Function*>(term->getParent()->getParent());

  //ProfileInfo &pi = getAnalysis< ProfileInfo >();
  BlockFrequencyInfo &bfi = getAnalysis< BlockFrequencyInfoWrapperPass >(*non_const_fcn).getBFI();
  BranchProbabilityInfo &bpi = getAnalysis< BranchProbabilityInfoWrapperPass >(*non_const_fcn).getBPI();
  //const double edge_cnt = pi.getEdgeWeight( ProfileInfo::Edge(term->getParent(), term->getSuccessor(sn) ) );
  if ( !bfi.getBlockProfileCount(term->getParent()).hasValue() )
    fout << "(unknown)";
  else
  {
    double edge_cnt = bpi.getEdgeProbability(term->getParent(),sn).scale(bfi.getBlockProfileCount(term->getParent()).getValue());
    fout << edge_cnt;
  }
}


/// This is an ugly hack.  Should we convert ControlSpeculation to an AnalysisGroup?
struct HoldRefToControlSpec : public ModulePass
{
  static char ID;
  HoldRefToControlSpec() : ModulePass(ID) {}

  StringRef getPassName() const { return "Hold reference to Profile-guided control speculation"; }

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< ProfileGuidedControlSpeculator >();
    au.setPreservesAll();
  }

  bool runOnModule(Module &mod) { return false; }
};

char HoldRefToControlSpec::ID=0;
static RegisterPass<HoldRefToControlSpec> hrtcs("hold-ref-to-control-spec", "Hold reference to control speculation");

}
}
