#include <fstream>

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/BitVector.h"

#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/KillFlow.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/PureFunAA.h"
#include "liberty/Analysis/SemiLocalFunAA.h"
#include "liberty/Orchestration/EdgeCountOracleAA.h"
#include "liberty/Speculation/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
//#include "liberty/Analysis/TXIOAA.h"
//#include "liberty/Analysis/CommutativeLibsAA.h"
//#include "liberty/Analysis/CommutativeGuessAA.h"
#include "liberty/Orchestration/PredictionSpeculation.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/PointsToAA.h"
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

namespace liberty
{
namespace SpecPriv
{
namespace FastDagSccExperiment
{
using namespace llvm;

static cl::opt<std::string> OutputWitness(
  "output-witness2-file-name", cl::init(""), cl::NotHidden,
  cl::desc("File name of witness"));

struct Exp_Witness2 : public ModulePass
{
  static char ID;
  Exp_Witness2() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< TargetLibraryInfoWrapperPass >();
    au.addRequired< ModuleLoops >();
    au.addRequired< LoopAA >();
    au.addRequired< Targets >();
    au.setPreservesAll();

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
  }

  bool runOnModule(Module &mod)
  {
    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    Targets &targets = getAnalysis< Targets >();

    // Compute our time base
    countCyclesPerSecond();

    std::ofstream fout( OutputWitness.c_str() );

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


    fout << "EOF\n";
    fout.close();

    return false;
  }

private:

  void runOnLoop(std::ofstream &fout, Loop *loop, ControlSpeculation *ctrlspec, PredictionSpeculation *predspec, Read *spresults, Classify *classify, const DataLayout &DL, PredictionAA *predaa, KillFlow_CtrlSpecAware *killflow_aware, CallsiteDepthCombinator_CtrlSpecAware *callsite_aware)
  {
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();

    Twine name = Twine(fcn->getName()) + " :: " + header->getName();
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


    LoopAA *aa = getAnalysis< LoopAA >().getRealTopAA();
    aa->stackHasChanged();

    aa->dump();

    typedef std::vector<Instruction*> IVec;
    IVec writes, reads;
    for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    {
      BasicBlock *bb = *i;
      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;
        if( inst->mayWriteToMemory() )
          writes.push_back(inst);               // May write and may read.
        else if( inst->mayReadFromMemory() )
          reads.push_back(inst);                // May read but not write.
      }
    }

    fout << fcn->getName().str() << ' ' << header->getName().str() << ' ' << writes.size() << ' ' << reads.size() << '\n';

    const uint64_t t_start_loop = rdtsc();
    const uint64_t timeout = Exp_Timeout * countCyclesPerSecond();

    const unsigned N=writes.size(), M=reads.size();
    // Write-vs-write
    for(unsigned i=0; i<N; ++i)
    {
      Instruction *src = writes[i];
      for(unsigned j=0; j<N; ++j)
      {
        Instruction *dst = writes[j];

        queries(aa,src,dst,loop,fout);

        if( rdtsc() - t_start_loop > timeout )
        {
          fout << "\nTIMEOUT\n";
          return;
        }
      }
      fout << '\n';
    }
    // Write-vs-read
    for(unsigned i=0; i<N; ++i)
    {
      Instruction *src = writes[i];
      for(unsigned j=0; j<M; ++j)
      {
        Instruction *dst = reads[j];

        queries(aa,src,dst,loop,fout);

        if( rdtsc() - t_start_loop > timeout )
        {
          fout << "\nTIMEOUT\n";
          return;
        }
      }
      fout << '\n';
    }
    // Read-vs-write
    for(unsigned i=0; i<M; ++i)
    {
      Instruction *src = reads[i];
      for(unsigned j=0; j<N; ++j)
      {
        Instruction *dst = writes[j];

        queries(aa,src,dst,loop,fout);

        if( rdtsc() - t_start_loop > timeout )
        {
          fout << "\nTIMEOUT\n";
          return;
        }
      }
      fout << '\n';
    }

    fout << '\n';
    fout.flush();


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

  void queries(LoopAA *aa, Instruction *src, Instruction *dst, Loop *loop, std::ofstream &fout) const
  {
    // Loop-carried queries
    query(aa,src, LoopAA::Before, dst, loop, fout);
    query(aa,dst, LoopAA::After,  src, loop, fout);

    // Intra-iteration queries
    query(aa,src, LoopAA::Same, dst, loop, fout);
    query(aa,dst, LoopAA::Same, src, loop, fout);
  }

  LoopAA::ModRefResult noLoopAA(Instruction *A, Instruction *B) const {
    if (!A->mayReadOrWriteMemory() || !B->mayReadOrWriteMemory())
      return LoopAA::NoModRef;
    else if (!A->mayReadFromMemory())
      return LoopAA::Mod;
    else if (!A->mayWriteToMemory())
      return LoopAA::Ref;
    else
      return LoopAA::ModRef;
  }

  void query(LoopAA *aa, Instruction *src, LoopAA::TemporalRelation R, Instruction *dst, Loop *L, std::ofstream &fout) const
  {
    Remedies Remeds;
    const uint64_t t0 = rdtsc();
    LoopAA::ModRefResult res = aa->modref(src,R,dst,L,Remeds);
    const uint64_t t1 = rdtsc();

    const double seconds = (t1 - t0) / (double) countCyclesPerSecond();

    // just for sanity check
    res = LoopAA::ModRefResult(res & noLoopAA(src, dst));

    std::string remediesStr = "";
    for (auto &r: Remeds) {
      remediesStr += r->getRemedyName().str() + ":";
    }

    //fout << '\n' << src << '\n';
    //fout << dst << '\n';
    fout << res << ' ' << seconds << ' ' << remediesStr << ' ';
  }
};

char Exp_Witness2::ID = 0;
static RegisterPass<Exp_Witness2> xxx("cgo20-witness2", "Witness2");
}
}
}
