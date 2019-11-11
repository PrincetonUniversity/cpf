#define DEBUG_TYPE "experiment1"

#include "llvm/Pass.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/FileSystem.h"

#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Orchestration/EdgeCountOracleAA.h"
#include "liberty/Analysis/PureFunAA.h"
#include "liberty/Analysis/SemiLocalFunAA.h"
#include "liberty/Analysis/KillFlow.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Speculation/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
//#include "liberty/Analysis/TXIOAA.h"
//#include "liberty/Analysis/CommutativeLibsAA.h"
//#include "liberty/Analysis/CommutativeGuessAA.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/PointsToAA.h"
#include "liberty/Orchestration/PredictionSpeculation.h"
#include "liberty/Orchestration/PtrResidueAA.h"
#include "liberty/Orchestration/ReadOnlyAA.h"
#include "liberty/Orchestration/ShortLivedAA.h"
#include "liberty/Orchestration/SmtxAA.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"
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

struct Exp_CGO20_Exhaustive : public ModulePass
{
  static char ID;
  Exp_CGO20_Exhaustive() : ModulePass(ID) {}

  StringRef getPassName() const { return "Timing-insensitive metrics; dumb"; }

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< TargetLibraryInfoWrapperPass >();
    au.addRequired< ModuleLoops >();
    au.addRequired< LoopAA >();
    au.addRequired< Targets >();
    if( UseCntrSpec )
      au.addRequired< ProfileGuidedControlSpeculator >();
    if ( UseValuePred )
      au.addRequired< ProfileGuidedPredictionSpeculator >();
    if( UseOracle )
      au.addRequired< SmtxSpeculationManager >();
    if( UsePtrResidue )
      au.addRequired< PtrResidueSpeculationManager >();
    if (UsePointsTo || UseRO || UseLocal) {
      au.addRequired<ReadPass>();
      au.addRequired<Classify>();
    }
    if (UseCntrSpec && UseCAF) {
      au.addRequired<KillFlow_CtrlSpecAware>();
      au.addRequired<CallsiteDepthCombinator_CtrlSpecAware>();
    }

    //if (((UsePointsTo || UseRO || UseLocal) && !UseCAF) || UseCAF) {
    au.addRequired<PureFunAA>();
    au.addRequired<SemiLocalFunAA>();
    au.addRequired<KillFlow>();
    //}

    au.setPreservesAll();
  }

  bool runOnModule(Module &mod)
  {
    countCyclesPerSecond();
    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    Targets &targets = getAnalysis< Targets >();

    std::error_code ec;
    raw_fd_ostream fout("cgo20-exhaustive-notiming.out", ec, sys::fs::F_RW);

    fout << "# BOF\n";
    fout << "$result ||= {}\n";
    fout << "$result['" << BenchName << "'] ||= {}\n";
    fout << "$result['" << BenchName << "']['" << OptLevel << "'] ||= {}\n";
    fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive'] ||= {}\n";
    fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "'] ||= {}\n";
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"'] ||= {}\n"; */
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"'] ||= {}\n"; */
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") << "'] ||= {}\n"; */
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") <<"']['"<<  (UseValuePred?"VALUE_PRED":"No VALUE_PRED") << "'] ||= {}\n"; */
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") <<"']['"<<  (UseValuePred?"VALUE_PRED":"No VALUE_PRED") << "']['" << (UsePtrResidue ? "PTR_RESIDUE" : "No PTR_RESIDUE") << "'] ||= {}\n"; */
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") <<"']['"<<  (UseValuePred?"VALUE_PRED":"No VALUE_PRED") << "']['" << (UsePtrResidue ? "PTR_RESIDUE" : "No PTR_RESIDUE") << "']['" << (UsePointsTo ? "POINTS_TO" : "No POINTS_TO") << "'] ||= {}\n"; */
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") <<"']['"<<  (UseValuePred?"VALUE_PRED":"No VALUE_PRED") << "']['" << (UsePtrResidue ? "PTR_RESIDUE" : "No PTR_RESIDUE") << "']['" << (UsePointsTo ? "POINTS_TO" : "No POINTS_TO") << "']['" << (UseRO ? "READ_ONLY" : "No READ_ONLY") << "'] ||= {}\n"; */
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") <<"']['"<<  (UseValuePred?"VALUE_PRED":"No VALUE_PRED") << "']['" << (UsePtrResidue ? "PTR_RESIDUE" : "No PTR_RESIDUE") << "']['" << (UsePointsTo ? "POINTS_TO" : "No POINTS_TO") << "']['" << (UseRO ? "READ_ONLY" : "No READ_ONLY") << "']['" << (UseLocal ? "USE_LOCAL" : "No USE_LOCAL") << "'] ||= {}\n"; */
    /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<<"']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") <<"']['"<<  (UseValuePred?"VALUE_PRED":"No VALUE_PRED") << "']['" << (UsePtrResidue ? "PTR_RESIDUE" : "No PTR_RESIDUE") << "']['" << (UsePointsTo ? "POINTS_TO" : "No POINTS_TO") << "']['" << (UseRO ? "READ_ONLY" : "No READ_ONLY") << "']['" << (UseLocal ? "USE_LOCAL" : "No USE_LOCAL") << "']['" << (UseCAF ? "CAF" : "No CAF") << "'] ||= {}\n"; */

    SmtxAA *smtxaa = 0;
    EdgeCountOracle *edgeaa = 0;
    PredictionAA *predaa = 0;
    ControlSpeculation *ctrlspec;
    PredictionSpeculation *predspec;
    PtrResidueAA *ptrresaa = 0;
    PointsToAA *pointstoaa = 0;
    //ReadOnlyAA *roaa = 0;
    //ShortLivedAA *localaa = 0;

    //TXIOAA txioaa;
    //CommutativeLibs commlibsaa;
    //CommutativeGuess commguessaa;

    const DataLayout& DL = mod.getDataLayout();

    if( UseOracle )
    {
      SmtxSpeculationManager &smtxMan = getAnalysis< SmtxSpeculationManager >();
      smtxaa = new SmtxAA(&smtxMan);
      smtxaa->InitializeLoopAA(this, DL);
    }

    if ( UseCntrSpec )
    {
      ctrlspec =
          getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
      edgeaa = new EdgeCountOracle(ctrlspec);
      edgeaa->InitializeLoopAA(this, DL);
    }

    if ( UseValuePred )
    {
      predspec = getAnalysis<ProfileGuidedPredictionSpeculator>()
                     .getPredictionSpecPtr();
      predaa = new PredictionAA(predspec);
      predaa->InitializeLoopAA(this, DL);
    }

    if ( UsePtrResidue )
    {
      PtrResidueSpeculationManager &ptrresMan = getAnalysis<PtrResidueSpeculationManager>();
      ptrresaa = new PtrResidueAA(DL, ptrresMan);
      ptrresaa->InitializeLoopAA(this, DL);
    }

    Read *spresults;
    Classify *classify;

    if ( UsePointsTo || UseRO || UseLocal)
    {
      spresults = &getAnalysis< ReadPass >().getProfileInfo();

      if (UsePointsTo) {
        pointstoaa = new PointsToAA(*spresults);
        pointstoaa->InitializeLoopAA(this, DL);
      }
    }

    if ( UseRO || UseLocal )
    {
      classify = &getAnalysis< Classify >();
    }

    KillFlow_CtrlSpecAware *killflow_aware;
    CallsiteDepthCombinator_CtrlSpecAware *callsite_aware;
    if (UseCntrSpec && UseCAF)
    {
      killflow_aware = &getAnalysis<KillFlow_CtrlSpecAware>();
      callsite_aware = &getAnalysis<CallsiteDepthCombinator_CtrlSpecAware>();
    }

    /*
    if ( UseTXIO )
      txioaa.InitializeLoopAA(this, DL);

    if ( UseCommLibs )
      commlibsaa.InitializeLoopAA(this, DL);

    if ( UseCommGuess )
      commguessaa.InitializeLoopAA(this, DL);
    */

    for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i)
      runOnLoop(fout, *i, ctrlspec, predspec, spresults, classify, DL, predaa, killflow_aware, callsite_aware);


    if( UseOracle )
      delete smtxaa;

    if ( UseCntrSpec )
      delete edgeaa;

    if ( UseValuePred )
      delete predaa;

    if ( UsePtrResidue )
      delete ptrresaa;

    if ( UsePointsTo )
      delete pointstoaa;

    fout << "# EOF\n";
    fout.flush();
    return false;
  }

private:

  void runOnLoop(raw_ostream &fout, Loop *loop, ControlSpeculation *ctrlspec, PredictionSpeculation *predspec, Read *spresults, Classify *classify, const DataLayout &DL, PredictionAA *predaa, KillFlow_CtrlSpecAware *killflow_aware, CallsiteDepthCombinator_CtrlSpecAware *callsite_aware)
  {
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();

    Twine nameT = Twine(fcn->getName()) + " :: " + header->getName();
    std::string name = nameT.str();
    errs() << "Working on " << name << "\n";

    NoControlSpeculation noctrlspec;
    NoPredictionSpeculation nopredspec;

    if ( UseCntrSpec )
      ctrlspec->setLoopOfInterest(loop->getHeader());
    else
      ctrlspec = &noctrlspec;

    if ( UseValuePred )
      predaa->setLoopOfInterest(loop);
    else
      predspec = &nopredspec;

    ReadOnlyAA *roaa = 0;
    ShortLivedAA *localaa = 0;

    if (UseRO || UseLocal) {
      const HeapAssignment &asgn = classify->getAssignmentFor(loop);
      // maybe need to check asgn.isValidFor(loop)
      /*
      if (!asgn.isValidFor(loop))
      {
        errs() << "ASSIGNMENT INVALID FOR LOOP: " << name << " in benchmark: " << BenchName  << "\n";
        return;
      }
      */

      const Ctx *ctx = spresults->getCtx(loop);

      if (UseRO) {
        roaa = new ReadOnlyAA(*spresults, asgn, ctx);
        roaa->InitializeLoopAA(this, DL);
      }
      if (UseLocal) {
        localaa = new ShortLivedAA(*spresults, asgn, ctx);
        localaa->InitializeLoopAA(this, DL);
      }
    }

    if (UseCntrSpec && UseCAF) {
      killflow_aware->setLoopOfInterest(ctrlspec, loop);
      callsite_aware->setLoopOfInterest(ctrlspec, loop);
    }

    //if ((UsePointsTo || UseRO || UseLocal) && !UseCAF) {
    if (!UseCAF) {
      PureFunAA &pure = getAnalysis<PureFunAA>();
      SemiLocalFunAA &semi = getAnalysis<SemiLocalFunAA>();
      KillFlow &kill = getAnalysis<KillFlow>();
      pure.disableQueryAnswers();
      semi.disableQueryAnswers();
      kill.disableQueryAnswers();
    }

    LoopAA *aa = getAnalysis< LoopAA >().getTopAA();
    aa->stackHasChanged();

    Vertices V(loop);

    aa->dump();

    /* std::string prefix = "$result['" + BenchName + "']['" + OptLevel + "']['" + AADesc + "']['" + (UseOracle ? "ORACLE" : "No ORACLE") + "']['" + name + "']['Exhaustive']"; */
    std::string prefix = "$result['" + BenchName + "']['" + OptLevel + "']['Exhaustive']['" + AADesc + "']['" + name + "']"; // don't need all the stuff since we can infer it from AADesc

/*
    bool mustRunAgainForNumQueries = false;
*/
    {
      Exp_PDG_NoTiming pdg(V,*ctrlspec,*predspec,aa->getDataLayout());
      pdg.setAA(aa);
      Exp_SCCs_NoTiming sccs(pdg);

      bool success = Exp_SCCs_NoTiming::computeDagScc_Dumb(pdg,sccs);

      /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<< "']['" << name << "']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") <<"']['"<<  (UseValuePred?"VALUE_PRED":"No VALUE_PRED") << "']['" << (UsePtrResidue ? "PTR_RESIDUE" : "No PTR_RESIDUE") << "']['" << (UsePointsTo ? "POINTS_TO" : "No POINTS_TO") << "']['" << (UseRO ? "READ_ONLY" : "No READ_ONLY") << "']['" << (UseLocal ? "USE_LOCAL" : "No USE_LOCAL") << "']['" << (UseCAF ? "CAF" : "No CAF") << "'] ||= {}\n"; */
      /* fout << "$result['" << BenchName << "']['" << OptLevel << "']['Exhaustive']['"<< AADesc << "']['"<< (UseOracle ? "ORACLE" : "No ORACLE")<< "']['" << name << "']['" << (UseCntrSpec?"CNTR_SPEC":"No CNTR_SPEC") <<"']['"<<  (UseValuePred?"VALUE_PRED":"No VALUE_PRED") << "']['" << (UsePtrResidue ? "PTR_RESIDUE" : "No PTR_RESIDUE") << "']['" << (UsePointsTo ? "POINTS_TO" : "No POINTS_TO") << "']['" << (UseRO ? "READ_ONLY" : "No READ_ONLY") << "']['" << (UseLocal ? "USE_LOCAL" : "No USE_LOCAL") << "']['" << (UseCAF ? "CAF" : "No CAF") << "'] ||= {}\n"; */
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
      fout << prefix << "['numComputeSCCs'] = " << sccs.numRecomputeSCCs << '\n';
      fout << prefix << "['nameQueriesSavedByRegCtrl'] = " << pdg.numQueriesSavedBecauseRedundantRegCtrl << '\n';

/*
      if( sccs.abortTimeout )
        mustRunAgainForNumQueries = true;
      else
*/
      {
        fout << prefix << "['numQueries'] = " << pdg.numQueries << '\n';
        fout << prefix << "['numNoModRefQueries'] = " << pdg.numNoModRefQueries << '\n';
        fout << prefix << "['numDepQueries'] = " << pdg.numDepQueries << '\n';
        fout << prefix << "['numPositiveDepQueries'] = " << pdg.numPositiveDepQueries << '\n';

        // Only compute backedge density if we didn't time-out and if we have
        // more than one SCC
        if( sccs.size() > 1 )
        {
          /* Disabled for now...
          unsigned numEdges=0, numMemEdges=0, numPositions=0;
          computeBackedgeDensity(pdg,sccs, numEdges, numMemEdges, numPositions);
          assert( numPositions > 0 );

          const float allBackEdgeDensity = numEdges / (float) numPositions;
          const float memBackEdgeDensity = numMemEdges / (float) numPositions;

          fout << prefix << "['allBackEdgeDensity'] = " << allBackEdgeDensity << '\n';
          fout << prefix << "['memBackEdgeDensity'] = " << memBackEdgeDensity << '\n';
          */
        }
      }
    }

/*
    // We want an accurate count of num queries even if construction would time-out
    if( mustRunAgainForNumQueries )
    {
      errs() << "Must run again for num queries\n";
      Exp_PDG_NoTiming pdg(V,ctrlspec,predspec,aa->getDataLayout());
      pdg.setAA(aa);
      Exp_SCCs_NoTiming sccs(pdg);

      Exp_SCCs_NoTiming::computeDagScc_Dumb_OnlyCountNumQueries(pdg,sccs);
      fout << prefix << "['numQueries'] = " << pdg.numQueries << '\n';
      fout << prefix << "['numNoModRefQueries'] = " << pdg.numNoModRefQueries << '\n';
      fout << prefix << "['numDepQueries'] = " << pdg.numDepQueries << '\n';
      fout << prefix << "['numPositiveDepQueries'] = " << pdg.numPositiveDepQueries << '\n';
    }
*/

    if ( UseRO )
      delete roaa;

    if ( UseLocal )
      delete localaa;

    if (!UseCAF) {
      PureFunAA &pure = getAnalysis<PureFunAA>();
      SemiLocalFunAA &semi = getAnalysis<SemiLocalFunAA>();
      KillFlow &kill = getAnalysis<KillFlow>();
      pure.enableQueryAnswers();
      semi.enableQueryAnswers();
      kill.enableQueryAnswers();
    }
  }

  void computeBackedgeDensity(const Exp_PDG_NoTiming &pdg, const Exp_SCCs_NoTiming &sccs, unsigned &numEdges, unsigned &numMemEdges, unsigned &numPositions) const
  {
    // Foreach SCC
    for(unsigned i=0, N=sccs.size(); i<N; ++i)
    {
      // List is in REVERSE top-order
      const Exp_SCCs_NoTiming::SCC &late = sccs.get(i);

      for(unsigned j=i+1; j<N; ++j)
      {
        // List is in REVERSE top-order
        const Exp_SCCs_NoTiming::SCC &early = sccs.get(j);

        countBackEdges(pdg, early, late, numEdges, numMemEdges, numPositions);
      }
    }

  }

  void countBackEdges(const Exp_PDG_NoTiming &pdg, const Exp_SCCs_NoTiming::SCC &early, const Exp_SCCs_NoTiming::SCC &late, unsigned &numEdges, unsigned &numMemEdges, unsigned &numPositions) const
  {
    const PartialEdgeSet &edgeset = pdg.getE();

    for(unsigned i=0, N=early.size(); i<N; ++i)
    {
      Vertices::ID ve = early[i];

      for(unsigned j=0, M=late.size(); j<M; ++j)
      {
        Vertices::ID vl = late[j];


        const PartialEdge &edge = edgeset.find(vl,ve);

        // Is there an edge?
        ++numPositions;
        if( edge.ii_reg || edge.ii_ctrl || edge.ii_mem
        ||  edge.lc_reg || edge.lc_ctrl || edge.lc_mem )
          ++numEdges;

        // Is there a memory edge?
        if( edge.ii_mem || edge.lc_mem )
          ++numMemEdges;
      }
    }
  }
};

char Exp_CGO20_Exhaustive::ID = 0;
static RegisterPass<Exp_CGO20_Exhaustive> xxx("cgo20-exhaustive", "Experiment: timing-insensitive metrics, dumb method");
}
}
}
