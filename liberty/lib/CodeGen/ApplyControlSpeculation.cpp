#define DEBUG_TYPE "specpriv-transform"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Support/FileSystem.h"

#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Selector.h"
#include "liberty/Redux/Reduction.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"
#include "liberty/Utilities/SplitEdge.h"
#include "liberty/Utilities/Timer.h"

#include "liberty/Speculation/Api.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Discriminator.h"
//#include "liberty/Speculation/PtrResidueManager.h"
#include "liberty/CodeGen/Preprocess.h"
#include "liberty/CodeGen/ApplyControlSpeculation.h"
#include "liberty/Speculation/Recovery.h"
#include "liberty/CodeGen/RoI.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

cl::opt<bool> DumpSpeculativeCFG(
  "specpriv-dump-speculative-cfg", cl::init(false), cl::Hidden,
  cl::desc("Dump speculative CFG to a .dot file"));

STATISTIC(numCtrlSpec,    "Speculatively dead control flow edges");
STATISTIC(numPhiEdgesCut, "Speculatively dead PHI-incoming register deps");

void ApplyControlSpec::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< ModuleLoops >();
  au.addRequired< BranchProbabilityInfoWrapperPass >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< Selector >();
  au.addRequired< Preprocess >();

  au.addRequired< BranchProbabilityInfoWrapperPass >();
  au.addPreserved< ModuleLoops >();
  au.addPreserved< ReadPass >();
  au.addPreserved< Selector >();
  au.addPreserved< Preprocess >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
  au.addPreserved< ProfileGuidedPredictionSpeculator >();
  //au.addPreserved< PtrResidueSpeculationManager >();
}

bool ApplyControlSpec::runOnModule(Module &module)
{
  DEBUG(errs() << "#################################################\n"
               << " ApplyControlSpec\n\n\n");
  mod = &module;
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  init(mloops);

  if( loops.empty() )
    return false;

  Preprocess &preprocess = getAnalysis< Preprocess >();
  preprocess.assert_strategies_consistent_with_ir();
  bool modified = false;

  // Control Speculation
  // - Insert control speculation checks.

  if( DumpSpeculativeCFG )
  {
    ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();

    // Foreach parallelized loop
    for(unsigned i=0, N=loops.size(); i<N; ++i)
    {
      BasicBlock *header = loops[i]->getHeader();
      Function *fcn = header->getParent();

      // For each function
      Twine filename = Twine("speccfg.") + fcn->getName() + ".wrt." + header->getName() + ".dot";
      errs() << "Writing " << filename << "...";

      //std::string errinfo;
      std::error_code ec;
      raw_fd_ostream fout(filename.str().c_str(), ec, sys::fs::F_RW);
      ctrlspec->setLoopOfInterest(header);
      ctrlspec->to_dot( fcn, mloops.getAnalysis_LoopInfo(fcn), fout );
      ctrlspec->setLoopOfInterest(0);

      errs() << " done.\n";
    }
  }

  // Global processing
  modified |= applyControlSpec(mloops);


  if( modified )
  {
    // Invalidate LoopInfo-analysis, since we have changed
    // the code.
    const RoI &roi = preprocess.getRoI();
    for(RoI::FSet::const_iterator i=roi.fcns.begin(), e=roi.fcns.end(); i!=e; ++i)
      mloops.forget(*i);

    preprocess.assert_strategies_consistent_with_ir();
    DEBUG(errs() << "Successfully applied speculation to sequential IR\n");
  }

  return modified;
}


void ApplyControlSpec::init(ModuleLoops &mloops)
{
  std::vector<Type *> formals;


  DEBUG(errs() << "SpecPriv ApplyControlSpec: Processing parallel region, consisting of:\n");


  // Identify loops we will parallelize
  const Selector &selector = getAnalysis< Selector >();
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    const BasicBlock *header = i->first;
    Function *fcn = const_cast< Function *>( header->getParent() );

    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);
    Loop *loop = li.getLoopFor(header);
    assert( loop->getHeader() == header );

    loops.push_back(loop);

    DEBUG(errs() << " - loop " << fcn->getName() << " :: " << header->getName() << "\n");
  }
}


bool ApplyControlSpec::applyControlSpec(ModuleLoops &mloops)
{
  bool modified = false;
  std::set< std::pair<TerminatorInst*, unsigned> > processed;

  Selector &selector = getAnalysis<Selector>();
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    const BasicBlock *loop_header = i->first;

    modified |= applyControlSpecToLoop( loop_header, processed, mloops);
  }

  return modified;
}

bool ApplyControlSpec::applyControlSpecToLoop(const BasicBlock *loop_header,
  std::set< std::pair<TerminatorInst*, unsigned> >& processed, ModuleLoops &mloops)
{
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(loop_header);

  Preprocess &preprocess = getAnalysis< Preprocess >();
  //ProfileInfo &pi = getAnalysis< ProfileInfo >();
  const Function* F = loop_header->getParent();
  // Evil, but okay because it won't modify the IR
  Function *non_const_F = const_cast<Function*>(F);
  BranchProbabilityInfo &bpi = getAnalysis< BranchProbabilityInfoWrapperPass >(*non_const_F).getBPI();

  LoopInfo &li = mloops.getAnalysis_LoopInfo(F);

  bool modified = false;
  RoI &roi = preprocess.getRoI();

  auto selectedCtrlSpecDeps = preprocess.getSelectedCtrlSpecDeps(loop_header);

  // Cut speculatively dead incoming values of PHI nodes.
  // For each PHI node in the RoI:
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      PHINode *phi = dyn_cast< PHINode >( &*j );
      if( !phi )
        break;

      const unsigned N=phi->getNumIncomingValues();
      for(unsigned k=0; k<N; ++k)
        if( ctrlspec->phiUseIsSpeculativelyDead(phi,k) )
        {
          // This PHI node does not use this incoming value.
          ++numPhiEdgesCut;
          phi->setIncomingValue( k, UndefValue::get( phi->getType() ) );
          modified = true;
        }
    }
  }

  // Cut speculated control flow edges.
  // For each speculatively dead edge in the RoI:
  RoI::BBSet new_blocks;
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
  {
    BasicBlock *pred = *i;
    TerminatorInst *term = pred->getTerminator();
    const unsigned N = term->getNumSuccessors();
    if( N < 2 )
      continue;

    Function *fcn = pred->getParent();
    Loop *lpred = li.getLoopFor(pred);

    for(unsigned sn=0; sn<N; ++sn)
      if( ctrlspec->isSpeculativelyDead(term,sn) )
      {
        // check if the edge is already processed
        if ( processed.count( std::make_pair(term,sn) ) )
          continue;
        processed.insert( std::make_pair(term,sn) );

        // (pred) -> (succ) is a speculatively dead edge.
        BasicBlock *succ = term->getSuccessor(sn);

        // speculating loop exits (for the loop of interest) only allows
        // removal of ctrl deps. Need to check if control spec was chosen as
        // the remedy for these to avoid unnecessary mis-speculation
        bool loopOfInterestExit = lpred && !lpred->contains(succ) &&
                                  lpred->getHeader() == loop_header;
        bool selectedCtrlSpecDep = selectedCtrlSpecDeps.count(term);

        if (loopOfInterestExit && !selectedCtrlSpecDep)
          continue;

        DEBUG(errs() << "Speculating edge is dead: " << fcn->getName() << " :: " << pred->getName()  << " successor " << sn << '\n');
        std::string message = ("Control misspeculation at " + fcn->getName() + " :: " + pred->getName() + " successor " + succ->getName()).str();
        Value *msg = getStringLiteralExpression(*mod, message);

        // Old edge weight
        //const double old_weight = pi.getEdgeWeight( ProfileInfo::Edge(pred,succ) );
        auto old_prob = bpi.getEdgeProbability(pred, sn);

        BasicBlock *splitedge = split(pred, sn, "specDead.");
        Instruction *misspec = CallInst::Create( Api(mod).getMisspeculate(), msg );
        InstInsertPt::Beginning(splitedge) << misspec;

        //TODO: figure out how spliiting works and make sure that these two are correct and I dont to iterate over all hte successors to find whihc is correct

        //ProfileInfo::Edge new_edge(pred,splitedge);
        //pi.setEdgeWeight(new_edge, old_weight);
        // sot: based on the split implementation, the sn successor should be the splitedge
        bpi.setEdgeProbability(pred, sn, old_prob);
        //new_edge = ProfileInfo::Edge(splitedge,succ);
        //pi.setEdgeWeight(new_edge, 0.0);
        // sot: splitedge only has one successor (succ), thus index = 0
        assert (splitedge->getTerminator()->getSuccessor(0) == succ);
        bpi.setEdgeProbability(splitedge, 0, BranchProbability::getZero());


        // Problem: if this edge is a loop exit,
        // we want the misspeculation check to occur WITHIN
        // the loop, so that we validate it during
        // parallel execution.
        // But, this code will place the validation check
        // OUTSIDE of the loop.
        // Difficulties: which loop?

        preprocess.addToLPS(misspec, term);
        preprocess.addToLPS(splitedge->getTerminator(), term);
        new_blocks.insert(splitedge);

        ++numCtrlSpec;
        modified = true;
      }
  }
  roi.bbs.insert( new_blocks.begin(), new_blocks.end() );

  return modified;
}

char ApplyControlSpec::ID = 0;
static RegisterPass<ApplyControlSpec> x("spec-priv-apply-control-spec",
  "Apply Control Speculation to RoI");
}
}
