#define DEBUG_TYPE "doall-transform"

#include "llvm/ADT/Statistic.h"
#include "liberty/CodeGen/DOALLTransform.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Recovery.h"

namespace liberty {
using namespace llvm;

STATISTIC(numLiveOuts,    "Live-out values demoted to private memory");

bool DOALLTransform::demoteLiveOutsAndPhis(Loop *loop, LiveoutStructure &liveoutStructure)
{
  // Identify a unique set of instructions within this loop
  // whose value is used outside of the loop.
  std::set<Instruction*> liveoutSet;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      for(Value::user_iterator k=inst->user_begin(), f=inst->user_end(); k!=f; ++k)
        if( Instruction *user = dyn_cast< Instruction >( *k ) )
          if( ! loop->contains(user) )
            liveoutSet.insert(inst);
    }
  }

  // Additionally, for recovery we will need the loop carried values.
  // Specifically, the values defined within the loop which are incoming
  // to a PHI.  We exclude the canonical induction variable, since we
  // handle that specially.
  LiveoutStructure::PhiList &phis = liveoutStructure.phis;
  BasicBlock *header = loop->getHeader();
  PHINode *civ = loop->getCanonicalInductionVariable();
  for(BasicBlock::iterator j=header->begin(), z=header->end(); j!=z; ++j)
  {
    PHINode *phi = dyn_cast< PHINode >( &*j );
    if( !phi )
      break;
    if (civ == phi)
      continue;
    phis.push_back(phi);
  }

  liveoutStructure.type = 0;
  liveoutStructure.object = 0;

  const unsigned N = liveoutSet.size();
  const unsigned M = phis.size();
  if( N + M < 1 )
    return false;

  // The liveouts, in a fixed order.
  LiveoutStructure::IList &liveouts = liveoutStructure.liveouts;
  liveouts.insert( liveouts.end(),
    liveoutSet.begin(), liveoutSet.end() );

  // Allocate a structure on the stack to hold all live-outs.
  LLVMContext &ctx = mod->getContext();
  std::vector<Type *> fields( N + M );
  for(unsigned i=0; i<N; ++i)
    fields[i] = liveouts[i]->getType();
  for(unsigned i=0; i<M; ++i)
    fields[N+i] = phis[i]->getType();
  StructType *structty = liveoutStructure.type = StructType::get(ctx, fields);

  Function *fcn = loop->getHeader()->getParent();
  AllocaInst *liveoutObject = new AllocaInst(structty, 0, "liveouts.from." + loop->getHeader()->getName());
  liveoutStructure.object = liveoutObject;
  InstInsertPt::Beginning(fcn) << liveoutObject;

  LLVM_LLVM_DEBUG(errs() << "Adding a liveout object " << *liveoutObject << " to function " << fcn->getName() << '\n');

  // After each definition of a live-out value, store it into the structure.
  Value *zero = ConstantInt::get(int32,0);
  for(unsigned i=0; i<N; ++i)
  {
    Instruction *def = liveouts[i];

    Value *indices[] = { zero, ConstantInt::get(int32, i) };
    GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(liveoutObject, ArrayRef<Value*>(&indices[0], &indices[2]));
    gep->setName( "liveout:" + def->getName() );
    StoreInst *store = new StoreInst(def, gep);

    InstInsertPt::After(def) << gep << store;

    // Add these new instructions to the partition
    //addToLPS(gep, def);
    //addToLPS(store, def);
  }

  // At each predecessor of the header
  // store the incoming value to the structure.
  for(unsigned i=0; i<M; ++i)
  {
    PHINode *phi = phis[i];

    Value *indices[] = { zero, ConstantInt::get(int32, N+i) };

    for(unsigned j=0; j<phi->getNumIncomingValues(); ++j)
    {
      BasicBlock *pred = phi->getIncomingBlock(j);

      GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(liveoutObject, ArrayRef<Value*>(&indices[0], &indices[2]));
      gep->setName( "phi-incoming:" + phi->getName() );
      Value *vdef = phi->getIncomingValue(j);
      StoreInst *store = new StoreInst( vdef, gep );

      InstInsertPt::End(pred) << gep << store;

      Instruction *gravity = phi;
      if( Instruction *idef = dyn_cast<Instruction>( vdef ) )
        gravity = idef;

      if( loop->contains(pred) )
      {
      //  addToLPS(gep, gravity);
       // addToLPS(store, gravity);
      }
    }
  }

  // TODO: replace loads from/stores to this structure with
  // API calls; allow the runtime to implement private semantics
  // as necessary.

  // Before each use of the live-out values which is NOT within the loop,
  // load it from the structure.
  // This /also/ means that the liveout structure must
  // have private semantics.
  for(unsigned i=0; i<N; ++i)
  {
    Instruction *def = liveouts[i];
    std::vector< User* > users( def->user_begin(), def->user_end() );

    for(unsigned j=0; j<users.size(); ++j)
      if( Instruction *user = dyn_cast< Instruction >( users[j] ) )
      {
        if( loop->contains(user) )
          continue;

        Value *indices[] = { zero, ConstantInt::get(int32, i) };

        // Either the use is a PHI, or not.
        if( PHINode *phi = dyn_cast< PHINode >(user) )
        {
          // It is a phi; we must split an edge :(
          for(unsigned k=0; k<phi->getNumIncomingValues(); ++k)
          {
            if( phi->getIncomingValue(k) != def )
              continue;

            BasicBlock *pred = phi->getIncomingBlock(k);
            BasicBlock *succ = phi->getParent();

            BasicBlock *splitedge = split(pred,succ,".unspill-liveouts.");

            GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(liveoutObject, ArrayRef<Value*>(&indices[0], &indices[2]) );
            gep->setName( "liveout:" + def->getName() );
            LoadInst *load = new LoadInst(gep);

            InstInsertPt::Beginning(splitedge) << gep << load;
            phi->setIncomingValue(k, load);
          }
        }
        else
        {
          GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(liveoutObject, ArrayRef<Value*>(&indices[0], &indices[2]) );
          gep->setName( "liveout:" + def->getName() );
          LoadInst *load = new LoadInst(gep);

          // Simple case: not a phi.
          InstInsertPt::Before(user) << gep << load;
          user->replaceUsesOfWith(def, load);
        }
      }
  }

  // The liveout structure must have private
  // semantics.  We rely on the runtime to accomplish
  // that.  With SMTX, this is automatic.
  // With Specpriv, we must mark that structure
  // as a member of the PRIVATE heap.
  ReadPass *rp = getAnalysisIfAvailable< ReadPass >();
  Selector *sps = getAnalysisIfAvailable< Selector >();
  if( rp && sps )
  {
    const Read &spresults = rp->getProfileInfo();
    Ptrs aus;
    Ctx *fcn_ctx = spresults.getCtx(fcn);
    assert( spresults.getUnderlyingAUs(liveoutObject, fcn_ctx, aus)
    && "Failed to create AU objects for the live-out object?!");

    HeapAssignment &asgn = sps->getAssignment();
    HeapAssignment::AUSet &privs = asgn.getPrivateAUs();
    for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
      privs.insert( i->au );
  }

  numLiveOuts += N;
  return true;
}
}
