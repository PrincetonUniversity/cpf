#define DEBUG_TYPE "spec-priv-ptr-residue-aa"

#include "llvm/ADT/Statistic.h"

#include "liberty/Speculation/PtrResidueManager.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numAssumptions, "Number of pointer residue sets we must assume / validate");

void PtrResidueSpeculationManager::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< ReadPass >();
  au.setPreservesAll();
}

bool PtrResidueSpeculationManager::runOnModule(Module &mod)
{
  spresults = & getAnalysis< ReadPass >().getProfileInfo();
  return false;
}

bool PtrResidueSpeculationManager::isAssumed(const Assumption &ass) const
{
  return assumptions.count(ass);
}

void PtrResidueSpeculationManager::setAssumed(const Assumption &ass)
{
  if( assumptions.insert(ass).second )
    ++numAssumptions;
}

void PtrResidueSpeculationManager::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - PtrResidueSpeculationManager::contextRenamedViaClone: " << *changedContext << '\n';

  Assumptions newAssumptions;

  for(iterator i=begin(), e=end(); i!=e; ++i)
  {
    // COPY, not reference
    Assumption ass = *i;

    const ValueToValueMapTy::const_iterator j = vmap.find( ass.first );
    if( vmap.end() != j )
      ass.first = j->second;

    const CtxToCtxMap::const_iterator k = cmap.find(ass.second);
    if( cmap.end() != k )
      ass.second = k->second;

    newAssumptions.insert(ass);
  }

  assumptions.swap(newAssumptions);
}

char PtrResidueSpeculationManager::ID = 0;
static RegisterPass< PtrResidueSpeculationManager > rp("spec-priv-ptr-residue-manager", "Pointer-residue manager");
}
}
