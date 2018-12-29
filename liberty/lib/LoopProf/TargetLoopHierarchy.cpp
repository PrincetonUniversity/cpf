#include "llvm/Analysis/LoopInfo.h"

#include "liberty/LoopProf/TargetLoopHierarchy.h"
#include "llvm/Support/raw_ostream.h"

namespace liberty
{

using namespace llvm;

bool TargetLoopHierarchy::runOnModule(Module& m)
{
  std::vector< std::vector<Loop*> > h_vec;

  // find a hierarchy between target loops
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Targets &targets = getAnalysis< Targets >();
  for(Targets::iterator ti = targets.begin(mloops), te = targets.end(mloops) ; ti != te ; ++ti)
  {
    Loop* loop = *ti;
    //if (loop->getHeader()->getParent()->getName() == "Perl_runops_standard")
    //  assert(false);
    if ( !hasHotSubloop(loop, targets) )
    {
      std::vector<Loop*> h;
      Loop* iter = loop;
      while (iter)
      {
        if ( std::find(targets.begin(), targets.end(), iter->getHeader()) != targets.end() )
          h.push_back(iter);
        iter = iter->getParentLoop();
      }
      h_vec.push_back(h);
    }
  }

  // print a hierarchy
  errs() << "*** Print Target Loop Hierarchy (size: " << h_vec.size() << ")\n";
  errs() << "(inner) ----------- (outer):\n";

  for (unsigned i = 0 ; i < h_vec.size() ; i++)
  {
    std::vector<Loop*>& h = h_vec[i];
    errs() << " - ";
    std::string fname = h[0]->getHeader()->getParent()->getName();
    errs() << fname << ":: ";
    for (unsigned j = 0 ; j < h.size() ; j++)
    {
      if (j != 0)
        errs() << " -> ";
      std::string name = h[j]->getHeader()->getName().str();
      errs() << name;
    }
    errs() << "\n";
  }

  return true;
}

bool TargetLoopHierarchy::hasHotSubloop(Loop* l, Targets& targets)
{
  const std::vector<Loop*>& subloops = l->getSubLoops();

  for (unsigned i = 0 ; i < subloops.size() ; i++)
  {
    Loop* subloop = subloops[i];
    if ( std::find(targets.begin(), targets.end(), subloop->getHeader()) != targets.end() )
      return true;

    if ( hasHotSubloop(subloop, targets) )
      return true;
  }

  return false;
}

char TargetLoopHierarchy::ID = 0;

namespace
{
  static RegisterPass<TargetLoopHierarchy>
    X("target-loop-hierarchy", "Print hierarchy of target loops", false, true);
}

}
