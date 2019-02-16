#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/LegacyPassManagers.h"

#include "liberty/Utilities/GimmeLoops.h"

namespace liberty
{

class  MyPMDataManager : public PMDataManager
{
  public:
    MyPMDataManager() : PMDataManager() {AvailableAnalysis.clear();}

    virtual Pass *getAsPass() { return 0; }

    mutable DenseMap<AnalysisID, const PassInfo *> AnalysisPassInfos;

    DenseMap<AnalysisID, Pass*> AvailableAnalysis;

    void recordAvailableAnalysis(Pass *P)  {
      AnalysisID PI = P->getPassID();

      AvailableAnalysis[PI] = P;

      assert(!AvailableAnalysis.empty());

      const PassInfo *PInf = PassRegistry::getPassRegistry()->getPassInfo(PI);
      if (PInf == 0) return;
      const std::vector<const PassInfo*> &II = PInf->getInterfacesImplemented();
      for (unsigned i = 0, e = II.size(); i != e; ++i)
        AvailableAnalysis[II[i]->getTypeInfo()] = P;
    }
};



GimmeLoops::~GimmeLoops()
{
  if( sep )
  {
    sep->releaseMemory();
    sep->doFinalization(*mod);
    delete sep;
  }
  if( lip )
  {
    lip->releaseMemory();
    lip->doFinalization(*mod);
    delete lip;
  }
  if( pdtp )
  {
    pdtp->releaseMemory();
    pdtp->doFinalization(*mod);
    delete pdtp;
  }
  if( dtp )
  {
    dtp->releaseMemory();
    dtp->doFinalization(*mod);
    delete dtp;
  }
  if( tlip ) delete tlip;
  if (act) delete act;
  if( ppp ) delete ppp;
}

void GimmeLoops::init(const DataLayout *target, TargetLibraryInfo *lib, Function *fcn, bool computeScalarEvolution)
{
  assert(fcn && "Null function argument in GimmeLoops init");
  mod = fcn->getParent();

  td = &mod->getDataLayout();
  tlip = new TargetLibraryInfoWrapperPass();
  dtp = new DominatorTreeWrapperPass();

  pdtp = new PostDominatorTreeWrapperPass();
  lip = new LoopInfoWrapperPass();

  act = new AssumptionCacheTracker();

  sep = new ScalarEvolutionWrapperPass();

  ppp = new MyPMDataManager();

  AnalysisResolver *ar= 0;

  ar = new AnalysisResolver(*ppp);

  tlip->setResolver(ar);

  // Add the implementations of the all the analyses required in
  // getAnalysisUsage.
  // This way it is known what should be returned on getAnalysis

  // in this case it seems that DominatorTreeWrapperPass did not need to be
  // added, nor the tgtLibInfo
  ar = new AnalysisResolver(*ppp);
  ar->addAnalysisImplsPair(&TargetLibraryInfoWrapperPass::ID, tlip);
  ar->addAnalysisImplsPair(&DominatorTreeWrapperPass::ID, dtp);
  pdtp->setResolver(ar);

  // lip seems to only need DominatorTreeWrapperPass (based on
  // LoopInfoWrapperPass::getAnalysisUsage())
  ar = new AnalysisResolver(*ppp);
  ar->addAnalysisImplsPair(&TargetLibraryInfoWrapperPass::ID, tlip);
  ar->addAnalysisImplsPair(&DominatorTreeWrapperPass::ID, dtp);
  ar->addAnalysisImplsPair(&PostDominatorTreeWrapperPass::ID, pdtp);
  lip->setResolver(ar);

  // needs all except for postdom
  ar = new AnalysisResolver(*ppp);
  ar->addAnalysisImplsPair(&TargetLibraryInfoWrapperPass::ID, tlip);
  ar->addAnalysisImplsPair(&DominatorTreeWrapperPass::ID, dtp);
  ar->addAnalysisImplsPair(&PostDominatorTreeWrapperPass::ID, pdtp);
  ar->addAnalysisImplsPair(&LoopInfoWrapperPass::ID, lip);
  ar->addAnalysisImplsPair(&AssumptionCacheTracker::ID, act);
  sep->setResolver(ar);

  tlip->doInitialization(*mod);
  dtp->doInitialization(*mod);
  pdtp->doInitialization(*mod);
  lip->doInitialization(*mod);
  sep->doInitialization(*mod);

  tlip->runOnModule(*mod);
  ppp->recordAvailableAnalysis(tlip);

  // run DT

  dtp->runOnFunction(*fcn);
  ppp->recordAvailableAnalysis(dtp);

  // run PDT

  pdtp->runOnFunction(*fcn);
  ppp->recordAvailableAnalysis(pdtp);

  // run LI

  lip->runOnFunction(*fcn);
  ppp->recordAvailableAnalysis(lip);

  // run SE

  sep->runOnFunction(*fcn);
  ppp->recordAvailableAnalysis(sep);

  //tli = &tlip->getTLI();
  dt = &dtp->getDomTree();
  pdt = &pdtp->getPostDomTree();
  li = &lip->getLoopInfo();
  se = &sep->getSE();
}

}
