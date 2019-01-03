#define DEBUG_TYPE "spec-priv-prediction-aa"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/Remat.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

char ProfileGuidedPredictionSpeculator::ID = 0;
static RegisterPass< ProfileGuidedPredictionSpeculator > rp("pred-spec", "Value-prediction Speculation Manager", false, false);

void ProfileGuidedPredictionSpeculator::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< ReadPass >();
  au.setPreservesAll();
}

ProfileGuidedPredictionSpeculator::load_iterator ProfileGuidedPredictionSpeculator::loads_begin(const Loop *loop) const
{
  return predictedLoads.lower_bound( loop->getHeader() );
}

ProfileGuidedPredictionSpeculator::load_iterator ProfileGuidedPredictionSpeculator::loads_end(const Loop *loop) const
{
  return predictedLoads.upper_bound( loop->getHeader() );
}

bool ProfileGuidedPredictionSpeculator::isPredictable(const Instruction *inst, const Loop *loop)
{
  if( !loop )
    return false;

  const LoadInst *load = dyn_cast< LoadInst >(inst);
  if( !load )
    return false;

  ProfileGuidedPredictionSpeculator::Loop2Load::value_type key( loop->getHeader(), load );

  const Read &read = getAnalysis< ReadPass >().getProfileInfo();
  if( !read.resultsValid() )
    return false;

  const Ctx *ctx = read.getCtx(loop);
  const Value *ptr = load->getPointerOperand();

  DEBUG(errs() << "Is predictable: " << *load << " at " << loop->getHeader()->getName() << "?\n");

  Function *fcn = loop->getHeader()->getParent();
  Remat remat;
  if( ! remat.canRematAtEntry(ptr,fcn) )
    return false;

  Ints ints;
  if( read.predictIntAtLoop(load,ctx,ints) )
    if( ints.size() == 1 )
    {
      DEBUG(errs() << "  + yes, predictable int\n");
      predictedLoads.insert( key );
      return true;
    }

  Ptrs ptrs;
  if( read.predictPtrAtLoop(load,ctx,ptrs) )
    if( ptrs.size() == 1 )
    {
      DEBUG(errs() << "  + yes, predictable ptr\n");
      predictedLoads.insert( key );
      return true;
    }
  return false;
}

void ProfileGuidedPredictionSpeculator::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - ProfileGuidedPredictionSpeculator::contextRenamedViaClone: " << *changedContext << '\n';
  if( changedContext->type != Ctx_Fcn )
    return;
  const Function *fcn = changedContext->getFcn();

  const ValueToValueMapTy::const_iterator vmap_end = vmap.end();
  assert( vmap.find(fcn) != vmap_end );

  Loop2Load newPredictions;
  for(Loop2Load::iterator i=predictedLoads.begin(), e=predictedLoads.end(); i!=e; ++i)
  {
    BasicBlock *header = i->first;
    const LoadInst *load = i->second;

    ValueToValueMapTy::const_iterator fheader = vmap.find(header),
                                      fload   = vmap.find(load);

    if( fheader == vmap_end && fload == vmap_end )
      continue;

//    errs() << "  - Was predicting: "
//           << "    H " << header->getParent()->getName() << "::" << header->getName() << '\n'
//           << "    L " << load->getParent()->getParent()->getName() << "::" << load->getParent()->getName() << ": " << *load << '\n';

    if( fheader != vmap_end )
      header = cast< BasicBlock >( &* (fheader->second) );

    if( fload != vmap_end )
      load = cast< LoadInst >( &* (fload->second) );

    newPredictions.insert( Loop2Load::value_type(header,load) );

//    errs() << "  - Now also predicting: "
//           << "    H " << header->getParent()->getName() << "::" << header->getName() << '\n'
//           << "    L " << load->getParent()->getParent()->getName() << "::" << load->getParent()->getName() << ": " << *load << '\n';
  }

  predictedLoads.insert( newPredictions.begin(), newPredictions.end() );
}

}
}

