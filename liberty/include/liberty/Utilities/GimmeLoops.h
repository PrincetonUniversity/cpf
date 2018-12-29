#ifndef GIMME_LOOPS_H
#define GIMME_LOOPS_H

namespace llvm
{
  class AnalysisResolver;
  class DataLayout;
  class TargetLibraryInfo;
  class TargetLibraryInfoWrapperPass;
  class DominatorTree;
  class DominatorTreeWrapperPass;
  class PostDominatorTree;
  class PostDominatorTreeWrapperPass;
  class LoopInfo;
  class LoopInfoWrapperPass;
  class ScalarEvolution;
  class ScalarEvolutionWrapperPass;
  class AssumptionCacheTracker;

  namespace legacy
  {
    class FunctionPassManager;
  }
}

namespace liberty
{
  using namespace llvm;

  struct MyPMDataManager;

  /// A horrible hack which slices through the bullshit
  /// of the pass manager.  No matter if you are in a
  /// loop pass or a function pass or whatever, you can
  /// now get domtree, loopinfo and scalarevolution information
  /// about any function you want.
  /// This is very inefficient, but it will work.
  struct GimmeLoops
  {
    GimmeLoops() : td(0), tli(0), dtp(0), dt(0), pdt(0), li(0), se(0), mod(0) { }

    GimmeLoops(const DataLayout *target, TargetLibraryInfo *lib, Function *fcn,
               bool computeScalarEvolution=false)
      : td(0), tli(0), dtp(0), dt(0), pdt(0), li(0), se(0), mod(0)
    {
      init(target,lib,fcn,computeScalarEvolution);
    }

    void init(const DataLayout *target, TargetLibraryInfo *lib, Function *fcn, bool computeScalarEvolution = false);

    ~GimmeLoops();

    /// Retrieve a DataLayout object
    const DataLayout *getTD()
    {
      return td;
    }

    /*
    TargetLibraryInfo *getTLI()
    {
      return tli;
    }
    */

    /// Retrieve the DominatorTree Analysis
    DominatorTree *getDT()
    {
      return dt;
    }

    /// Retrieve the PostDominatorTree analysis
    PostDominatorTree *getPDT()
    {
      return pdt;
    }

    /// Retrieve the LoopInfo analysis
    LoopInfo *getLI()
    {
      return li;
    }

    /// Retrieve the ScalarEvolution analysis
    ScalarEvolution *getSE()
    {
      return se;
    }

  private:
    const DataLayout *td;
    TargetLibraryInfo *tli;
    TargetLibraryInfoWrapperPass *tlip;
    DominatorTreeWrapperPass *dtp;
    DominatorTree *dt;
    PostDominatorTreeWrapperPass *pdtp;
    PostDominatorTree *pdt;
    LoopInfoWrapperPass *lip;
    LoopInfo *li;
    ScalarEvolutionWrapperPass *sep;
    ScalarEvolution *se;
    AssumptionCacheTracker *act;

    MyPMDataManager *ppp;
    Module *mod;
  };
}

#endif

