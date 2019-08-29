#define DEBUG_TYPE "experiment1"

#include "llvm/Pass.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/FileSystem.h"

#include "Pipeline.h"
#include "ProfilePerformanceEstimator.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/PredictionSpeculation.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/PointsToAA.h"
#include "liberty/Orchestration/PtrResidueAA.h"
#include "liberty/Orchestration/ReadOnlyAA.h"
#include "liberty/Orchestration/ShortLivedAA.h"
#include "liberty/Orchestration/SmtxAA.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/MakePtr.h"

#include "Exp.h"
//#include "RoI.h"
#include "Exp_PDG_NoTiming.h"
#include "Exp_DAGSCC_NoTiming.h"

namespace liberty
{
namespace SpecPriv
{
namespace FastDagSccExperiment
{
using namespace llvm;

struct Exp_CGO20_FastDagScc : public ModulePass
{
  static char ID;
  Exp_CGO20_FastDagScc() : ModulePass(ID) {}

  StringRef getPassName() const { return "Timing-insensitive metrics; PS-DSWP client"; }

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    //au.addRequired< DataLayout >();
    au.addRequired< TargetLibraryInfoWrapperPass >();
    au.addRequired< ModuleLoops >();
    au.addRequired< LoopAA >();
    au.addRequired< Targets >();
    au.addRequired< SmtxSpeculationManager >();
    au.addRequired< ProfilePerformanceEstimatorOLD >();
    au.addRequired< ProfileGuidedControlSpeculator >();
    au.addRequired< ProfileGuidedPredictionSpeculator >();
    au.setPreservesAll();
  }

  bool runOnModule(Module &mod)
  {
    countCyclesPerSecond();
    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    Targets &targets = getAnalysis< Targets >();

    std::error_code ec;
    raw_fd_ostream fout("cgo20-psdswp-notiming.out", ec, sys::fs::F_RW);

    fout << "# BOF\n";
    fout << "$result ||= {}\n";
    fout << "$result['" << BenchName << "'] ||= {}\n";
    fout << "$result['" << BenchName << "']['"<< OptLevel <<"'] ||= {}\n";
    fout << "$result['" << BenchName << "']['"<< OptLevel <<"']['"<<AADesc<<"'] ||= {}\n";
    fout << "$result['" << BenchName << "']['"<< OptLevel <<"']['"<<AADesc<<"']['"<<(UseOracle?"ORACLE":"No ORACLE")<<"'] ||= {}\n";
    fout << "$result['" << BenchName << "']['"<< OptLevel <<"']['"<<AADesc<<"']['"<<(UseOracle?"ORACLE":"No ORACLE")<<"']['"<<(HideContext?"No Context" : "Contextualized")<<"'] ||= {}\n";

    SmtxAA *smtxaa = 0;
    EdgeCountOracle *edgeaa = 0;
    PredictionAA *predaa = 0;
//    EdgeCountOracle *edgeaa = 0;

    ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
    PredictionSpeculation *predspec = getAnalysis< ProfileGuidedPredictionSpeculator >().getPredictionSpecPtr();

    NoControlSpeculation noctrlspec;
    NoPredictionSpeculation nopredspec;

    // NoControlSpeculation noctrl;
    // ctrlspec = &noctrl;

    if( UseOracle )
    {
      SmtxSpeculationManager &smtxMan = getAnalysis< SmtxSpeculationManager >();
      smtxaa = new SmtxAA(&smtxMan);
      const DataLayout& DL = mod.getDataLayout();
      smtxaa->InitializeLoopAA(this, DL);

//      ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
//      edgeaa = new EdgeCountOracle(ctrlspec);
//      edgeaa->InitializeLoopAA(this);
    }
    /*
    if ( UseSpecAdaptors )
    {
      const DataLayout& DL = mod.getDataLayout();

      edgeaa = new EdgeCountOracle(ctrlspec);
      edgeaa->InitializeLoopAA(this, DL);

      predaa = new PredictionAA(predspec);
      predaa->InitializeLoopAA(this, DL);
    }
    else
    {
      ctrlspec = &noctrlspec;
      predspec = &nopredspec;
    }
    */

    for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i)
      runOnLoop(fout, *i, predspec);

    if( UseOracle )
    {
//      delete edgeaa;
      delete smtxaa;
    }

    fout << "# EOF\n";
    fout.flush();
    return false;
  }

private:
  ControlSpeculation *ctrlspec;

  void runOnLoop(raw_ostream &fout, Loop *loop, PredictionSpeculation *predspec)
  {
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();

    Twine nameT = Twine(fcn->getName()) + " :: " + header->getName();
    std::string name = nameT.str();
    errs() << "Working on " << name << "\n";

    LoopAA *aa = getAnalysis< LoopAA >().getTopAA();
    aa->stackHasChanged();
    aa->dump();

    // PredictionSpeculation *predspec = (getAnalysis< ProfileGuidedPredictionSpeculator >().getPredictionSpecPtr());
    // NoPredictionSpeculation nopredspec;

    /* if ( UseSpecAdaptors )
    {
      ctrlspec->setLoopOfInterest(header);
      const DataLayout& DL = fcn->getParent()->getDataLayout();

      EdgeCountOracle edgeaa(ctrlspec);
      edgeaa.InitializeLoopAA(this, DL);

      PredictionAA predaa(predspec);
      predaa.InitializeLoopAA(this, DL);
    }
    else
      predspec = &nopredspec; */

    // LoopAA *aa = 0;
    // aa->stackHasChanged();

    // if ( UseSpecAdaptors )
    // {
    //   ctrlspec->setLoopOfInterest(header);
    //   const DataLayout& DL = fcn->getParent()->getDataLayout();

    //   EdgeCountOracle edgeaa(ctrlspec);
    //   edgeaa.InitializeLoopAA(this, DL);

    //   PredictionAA predaa(predspec);
    //   predaa.InitializeLoopAA(this, DL);

    //   aa = predaa.getTopAA();
    // }
    // else
    // {
    //   predspec = &nopredspec;

    //   aa = getAnalysis<LoopAA>().getTopAA();
    // }
    Vertices V(loop);
   /*  NoPredictionSpeculation predspec;

    if ( UseSpecAdaptors )
    {
      ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
      ctrlspec->setLoopOfInterest(header);
      ProfileGuidedPredictionSpeculator &predspec = getAnalysis< ProfileGuidedPredictionSpeculator >();
      const DataLayout &td = fcn->getParent()->getDataLayout();

      EdgeCountOracle edgeaa(ctrlspec);
      edgeaa.InitializeLoopAA(this, td);

      PredictionAA predaa(&predspec);
      predaa.InitializeLoopAA(this, td);

      aa = predaa.getTopAA();
      aa->stackHasChanged();
    } */

    const DataLayout* DL = &fcn->getParent()->getDataLayout();
    Exp_PDG_NoTiming pdg(V,*ctrlspec,*predspec, DL);
    pdg.setAA(aa);
    Exp_SCCs_NoTiming sccs(pdg);

    bool success = Exp_SCCs_NoTiming::computeDagScc(pdg,sccs);

    fout << "$result['" << BenchName << "']['"<<OptLevel<<"']['"<<AADesc<<"']['"<<(UseOracle?"ORACLE":"No ORACLE")<<"']['"<<(HideContext ? "No Context" : "Contextualized")<<"']['" << name << "'] ||= {}\n";

    std::string prefix = "$result['" + BenchName + "']['" + OptLevel + "']['"+AADesc+"']['"+(UseOracle?"ORACLE":"No ORACLE")+"']['"+(HideContext?"No Context":"Contextualized")+"']['" + name + "']['PS-DSWP Client']";
    fout << prefix << " ||= {}\n";

    if( sccs.abortTimeout )
      fout << prefix << "['result'] = 'abort-timeout'\n";
    else if( success )
      fout << prefix << "['result'] = 'ok'\n";
    else
      fout << prefix << "['result'] = 'bail-out'\n";

    fout << prefix << "['numV'] = " << pdg.numVertices() << '\n';
    fout << prefix << "['numE'] = " << pdg.getE().size() << '\n';
    fout << prefix << "['numSCCs'] = " << sccs.size() << '\n';
    fout << prefix << "['numDoallSCCs'] = " << sccs.numDoallSCCs() << '\n';
    fout << prefix << "['numQueries'] = " << pdg.numQueries << '\n';
    fout << prefix << "['numNoModRefQueries'] = " << pdg.numNoModRefQueries << '\n';
    fout << prefix << "['numDepQueries'] = " << pdg.numDepQueries << '\n';
    fout << prefix << "['numPositiveDepQueries'] = " << pdg.numPositiveDepQueries << '\n';
    fout << prefix << "['numComputeSCCs'] = " << sccs.numRecomputeSCCs << '\n';
    fout << prefix << "['nameQueriesSavedByRegCtrl'] = " << pdg.numQueriesSavedBecauseRedundantRegCtrl << '\n';
    fout << prefix << "['version2'] = true\n";

    unsigned numVerticesInPStage = 0;
    // If we successfully built a PDG, also try to generate
    // a PS-DSWP pipeline so we can determine the fraction of
    // the loop which is assigned to parallel stages.
    if( success && ! sccs.abortTimeout )
    {
      NoPredictionSpeculation nopredspec;
      PipelineStrategyOLD strat;

      ProfilePerformanceEstimatorOLD &perf = getAnalysis< ProfilePerformanceEstimatorOLD >();

      PDG normal_pdg = pdg.toNormalPDG();
      SCCs normal_sccs = sccs.toNormalSCCs();

      success = Pipeline::suggest(loop, normal_pdg, normal_sccs, perf, strat);
      if( success )
        // Count how many vertices...
        for(unsigned stageno=0, N=strat.stages.size(); stageno<N; ++stageno)
          // ...were assigned to a parallel stage
          if( strat.stages[ stageno ].type == PipelineStageOLD::Parallel )
            numVerticesInPStage += strat.stages[ stageno ].instructions.size();
    }

    fout << prefix << "['numVInAParallelStage'] = " << numVerticesInPStage << '\n';
  }

};

char Exp_CGO20_FastDagScc::ID = 0;
static RegisterPass<Exp_CGO20_FastDagScc> xxx("cgo20-fastdagscc", "Experiment: timing-insensitive metrics, PS-DSWP client");
}
}
}

