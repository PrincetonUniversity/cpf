#define DEBUG_TYPE "specpriv-transform"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Selector.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"

// For debugging
#include "llvm/IR/InstIterator.h"
#include "Metadata.h"
#include <sstream>

#include "liberty/Speculation/Recovery.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

void LiveoutStructure::print(raw_ostream &fout) const
{
  fout << "Liveout Structure:\n"
       << " - type " << *type << '\n'
       << " - object " << *object << '\n';

  fout << " - liveouts:\n";
  for(unsigned i=0; i<liveouts.size(); ++i)
    fout << "   o " << *liveouts[i] << '\n';

  fout << " - phis:\n";
  for(unsigned i=0; i<phis.size(); ++i)
    fout << "   o " << *phis[i] << '\n';
}

void RecoveryFunction::print(raw_ostream &fout) const
{
  fout << "Recovery Function:\n"
       << " - fcn " << fcn->getName() << '\n'
       << " - type " << *fcn->getType() << '\n';

  fout << " - liveins:\n";
  for(unsigned i=0; i<liveins.size(); ++i)
    fout << "   o " << *liveins[i] << '\n';

  liveoutStructure.print(fout);

  fout << " - exits:\n";
  for(CtrlEdgeNumbers::const_iterator i=exitNumbers.begin(), e=exitNumbers.end(); i!=e; ++i)
  {
    const CtrlEdge &edge = i->first;
    unsigned num = i->second;

    fout << "   o " << num << ": block " << edge.first->getName() << " successor #" << edge.second << '\n';
  }
}

void RecoveryFunction::dump() const
{
  print( errs() );
}

void Recovery::print(raw_ostream &fout) const
{
  fout << "----------------------------\n";
  fout << "Recovery Functions:\n"
       << " - There are " << recoveryFunctions.size() << " in total\n";

  for(Loop2Recovery::const_iterator i=recoveryFunctions.begin(), e=recoveryFunctions.end(); i!=e; ++i)
  {
    BasicBlock *header = i->first;
    Function *fcn = header->getParent();
    const RecoveryFunction &recovery = i->second;

    fout << "  === Loop " << fcn->getName() << " :: " << header->getName() << " ===\n";
    recovery.print(fout);
    fout << '\n';
  }

  fout << "----------------------------\n";
}

void Recovery::dump() const
{
  print( errs() );
}

void LiveoutStructure::replaceAllUsesOfWith(Value *oldv, Value *newv)
{
  if( Instruction *newinst = dyn_cast< Instruction >(newv) )
    for(unsigned i=0; i<liveouts.size(); ++i)
      if( oldv == liveouts[i] )
        liveouts[i] = newinst;

  if( PHINode *newphi = dyn_cast< PHINode >(newv) )
    for(unsigned i=0; i<phis.size(); ++i)
      if( oldv == phis[i] )
        phis[i] = newphi;

  if( Instruction *newinst = dyn_cast< Instruction >(newv) )
    for(unsigned i=0; i<reduxLiveouts.size(); ++i)
      if( oldv == reduxLiveouts[i] )
        reduxLiveouts[i] = newinst;

  if( Instruction *newinst = dyn_cast< Instruction >(newv) )
    if( oldv == object )
      object = newinst;

  if( Instruction *newinst = dyn_cast< Instruction >(newv) )
    for(unsigned i=0; i<reduxObjects.size();++i)
    if( oldv == reduxObjects[i] )
      reduxObjects[i] = newinst;
}

void RecoveryFunction::replaceAllUsesOfWith(Value *oldv, Value *newv)
{
  for(unsigned i=0; i<liveins.size(); ++i)
    if( oldv == liveins[i] )
      liveins[i] = newv;

  liveoutStructure.replaceAllUsesOfWith(oldv,newv);
}

void Recovery::replaceAllUsesOfWith(Value *oldv, Value *newv)
{
  for(Loop2Recovery::iterator i=recoveryFunctions.begin(), e=recoveryFunctions.end(); i!=e; ++i)
    i->second.replaceAllUsesOfWith(oldv,newv);
}

const RecoveryFunction &Recovery::getRecoveryFunction(Loop *loop) const
{
  BasicBlock *header = loop->getHeader();

  Loop2Recovery::const_iterator i = recoveryFunctions.find(header);
  assert( i != recoveryFunctions.end() );

  return i->second;
}

RecoveryFunction &Recovery::getRecoveryFunction(Loop *loop, ModuleLoops &mloops, const LiveoutStructure &liveoutStructure)
{
  BasicBlock *header = loop->getHeader();

  // Already computed?
  if( recoveryFunctions.count(header) )
    return recoveryFunctions[header];
  RecoveryFunction &recovery = recoveryFunctions[ header ];

  Function *fcn = header->getParent();
  Module *mod = fcn->getParent();

  // First clone the function.
  ValueToValueMapTy orig2clone;
  //Function *clone_fcn = CloneFunction(fcn, orig2clone, false, 0);
  Function *clone_fcn = CloneFunction(fcn, orig2clone);
  // sot: CloneFunction in LLVM 5.0 inserts the cloned function in the function's module
  //mod->getFunctionList().push_back(clone_fcn); // temporary

  // Name of the loop header in the clone.
  BasicBlock *clone_header = cast< BasicBlock >( &*orig2clone[header] );

  LoopInfo &clone_li = mloops.getAnalysis_LoopInfo(clone_fcn);
  DominatorTree &clone_dt = mloops.getAnalysis_DominatorTree(clone_fcn);

  // Find the loop within the cloned function.
  Loop *clone_loop = clone_li.getLoopFor( clone_header );
  assert( clone_loop->getHeader() == clone_header );

  // Extract the clone of the loop into a new function.
  Function *loop_fcn =
    CodeExtractor(clone_dt, *clone_loop, false).extractCodeRegion();

  // This new function has exactly one use: the call to
  // the extracted loop function.
  assert( loop_fcn->hasOneUse() && "Extract loop is weird");
  User *use = &* loop_fcn->user_back();
  CallSite cs = getCallSite(use);
  assert( cs.getInstruction() && "Extract loop is weird");
  assert( cs.getCalledFunction() == loop_fcn && "Extract loop is weird");

  // Figure out what the args to this loop mean.
  recovery.liveins.clear();
  for(unsigned i=0; i<cs.arg_size(); ++i)
  {
    // The argument is the image of some value
    // in the value map.
    Value *image = cs.getArgument(i);

    // Find the preimage.
    Value *preimage = 0;
    for( ValueToValueMapTy::const_iterator j=orig2clone.begin(), z=orig2clone.end(); j!=z; ++j)
      if( j->second == image )
      {
        preimage = const_cast< Value* >(j->first);
        break;
      }
    if (!preimage) {
      // TODO: properly handle live-outs. Allocas are produced in
      // lib/Transforms/Utils/CodeExtractor.cpp in llvm (line 694)
      // In the past all live-outs were demoted to memory, so all arguments were
      // live-ins. Now some of the args are liveouts transformed to allocas
      // (around the call sites)
      if (image && isa<AllocaInst>(image)) {
        DEBUG(errs() << "preimage of arg in getRecovery " << *image
                     << " is not found. Arg refers to a live-out.\n");
        continue;
      }
    }
    assert( preimage && "What is the argument?");
//    errs() << "Livein: " << *preimage << '\n';
    recovery.liveins.push_back(preimage);
  }

  // Next, we need to modify the argument list of the recovery function
  // so that it accepts two additional parameters, which correspond
  // to the [first,last] iterations to execute, inclusive.
  // To do this, we must create a new function, and clone
  // our function into that :(
  assert( !loop_fcn->getFunctionType()->isVarArg() && "Extract loop is weird");

  LLVMContext &ctx = mod->getContext();
  IntegerType *u32 = IntegerType::getInt32Ty(ctx);

  std::vector<Type *> formals;
  // [first,last]
  formals.push_back(u32);
  formals.push_back(u32);
  // initial values for the PHIs
  for(LiveoutStructure::PhiList::const_iterator i=liveoutStructure.phis.begin(), e=liveoutStructure.phis.end(); i!=e; ++i)
    formals.push_back( (*i)->getType() );

  for(LiveoutStructure::IList::const_iterator i=liveoutStructure.reduxLiveouts.begin(), e=liveoutStructure.reduxLiveouts.end(); i!=e; ++i)
    formals.push_back( (*i)->getType() );

  // other live-ins
  FunctionType *old_fty = loop_fcn->getFunctionType();
  formals.insert( formals.end(),
    old_fty->param_begin(), old_fty->param_end() );

  DEBUG(errs() << "formals.size: " << formals.size() << "\n";
        for (auto f
             : formals) errs()
        << "formal: " << *f << "\n";);

  FunctionType *new_fty = FunctionType::get(u32, formals, false);
  // errs() << "Fty: " << *new_fty << '\n';

  // new function...
  Function *rcvy = Function::Create(
    new_fty,
    fcn->getLinkage(),//GlobalValue::InternalLinkage,
    "misspeculation.recovery." + fcn->getName() + "." + header->getName(),
    mod);

  // Map the formals between old and new functions.
  Function::arg_iterator old_i = loop_fcn->arg_begin(),
                         new_i = rcvy->arg_begin(),
                         new_e = rcvy->arg_end();
  new_i->setName("low_iter");
  ++new_i;
  new_i->setName("high_iter");
  ++new_i;

  for(LiveoutStructure::PhiList::const_iterator j=liveoutStructure.phis.begin(), z=liveoutStructure.phis.end(); j!=z; ++j, ++new_i)
    new_i->setName("initial:" + (*j)->getName() );

  for(LiveoutStructure::IList::const_iterator j=liveoutStructure.reduxLiveouts.begin(), z=liveoutStructure.reduxLiveouts.end(); j!=z; ++j, ++new_i)
    new_i->setName("initial:" + (*j)->getName() );

  ValueToValueMapTy clone2recovery;
  for(; new_i != new_e; ++new_i, ++old_i)
  {
    Argument *old_arg = &*old_i;
    Argument *new_arg = &*new_i;

    new_arg->setName( old_arg->getName() );
    clone2recovery[ old_arg ] = new_arg;
  }

  // Clone the function into that.
  typedef SmallVector<ReturnInst*,4> Returns;
  Returns returns;
  CloneFunctionInto(rcvy, loop_fcn, clone2recovery, false, returns);
  rcvy->setLinkage( GlobalValue::InternalLinkage );

  // Sweet hibity gibity, that was a pain in the ass.
  assert( rcvy->getParent() != 0 );

  // Assign a canonical numbering of loop exit edges.
  // Update the recovery function so it returns the
  // right code at each exit.
  SmallVector<BasicBlock*,4> exitingBlocks;
  loop->getExitingBlocks(exitingBlocks);
  for(unsigned i=0; i<exitingBlocks.size(); ++i)
  {
    BasicBlock *exiting = exitingBlocks[i];
    TerminatorInst *term = exiting->getTerminator();

    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      BasicBlock *succ = term->getSuccessor(sn);
      if( loop->contains(succ) )
        continue;

      const unsigned id = recovery.exitNumbers.size() + 1;
      const RecoveryFunction::CtrlEdge edge = RecoveryFunction::CtrlEdge(term,sn);
      recovery.exitNumbers[ edge ] = id;
      recovery.exitDests[ edge ] = term->getSuccessor(sn);

      // Now, find this in the recovery function.
      BasicBlock *exitingInClone = dyn_cast< BasicBlock >( &*orig2clone[ exiting ] );
      assert( exitingInClone && "Exiting block didn't get copied to clone?!");
      BasicBlock *exitingInRecovery = dyn_cast< BasicBlock >( &*clone2recovery[ exitingInClone ] );
      assert( exitingInRecovery && "Exiting block didn't get copied to recovery!?");

      TerminatorInst *termInRecovery = exitingInRecovery->getTerminator();
      assert( termInRecovery->getNumSuccessors() == N && "Cloning messed up exiting edges?!");
      BasicBlock *succInRecovery = termInRecovery->getSuccessor(sn);

      // TODO: make sure that removing this next assertion is correct.
      // need to remove this assertion since now the live-outs are stored
      // to memory on loop exits and not on every iteration
      //assert( succInRecovery->size() == 1 && "Exit block in recovery should contain nothing but a return");

      ReturnInst *returnInRecovery = dyn_cast< ReturnInst >( succInRecovery->getTerminator() );
      assert( returnInRecovery && "Exit block's terminator in recovery should be a return");

      // Change it to return our code.
      Constant *rval = ConstantInt::get(u32,id);
      ReturnInst::Create(ctx, rval, returnInRecovery);

      // Remove the old return instruction, both
      // from the function, and from the list of returns.
      for(unsigned j=0; j<returns.size(); ++j)
        if( returns[j] == returnInRecovery )
        {
          std::swap( returns[j], returns.back() );
          returns.pop_back();
          break;
        }

      returnInRecovery->eraseFromParent();
    }
  }

  // Returns should be empty!
  assert( returns.empty() && "We missed a return!?");

  // Next, we should transform the loop function so that
  // it only iterates when we want it to.
  LoopInfo &rcvy_li = mloops.getAnalysis_LoopInfo( rcvy );
  Loop *rcvy_loop = * rcvy_li.begin();
  BasicBlock *rcvy_header = rcvy_loop->getHeader();

  // Update loop PHI nodes so that they load their initial values.
  Function::arg_iterator i=rcvy->arg_begin();
  ++i; ++i;
  for(LiveoutStructure::PhiList::const_iterator j=liveoutStructure.phis.begin(), z=liveoutStructure.phis.end(); j!=z; ++j, ++i)
  {
    PHINode *old_phi = *j;
    PHINode *middle_phi = dyn_cast< PHINode >( &*orig2clone[old_phi] );
    assert( middle_phi );
    PHINode *new_phi = dyn_cast< PHINode >( &*clone2recovery[ middle_phi ] );
    assert( new_phi );

    for(unsigned pn=0, N=new_phi->getNumIncomingValues(); pn<N; ++pn)
    {
      if( new_phi->getIncomingBlock(pn) != & rcvy->getEntryBlock() )
        continue;

      new_phi->setIncomingValue(pn, &*i);
    }
  }
  for(LiveoutStructure::IList::const_iterator j=liveoutStructure.reduxLiveouts.begin(), z=liveoutStructure.reduxLiveouts.end(); j!=z; ++j, ++i)
  {
    PHINode *old_phi = dyn_cast<PHINode>(*j);
    assert(old_phi && "Redux variable not a phi?");
    PHINode *middle_phi = dyn_cast< PHINode >( &*orig2clone[old_phi] );
    assert( middle_phi );
    PHINode *new_phi = dyn_cast< PHINode >( &*clone2recovery[ middle_phi ] );
    assert( new_phi );

    for(unsigned pn=0, N=new_phi->getNumIncomingValues(); pn<N; ++pn)
    {
      if( new_phi->getIncomingBlock(pn) != & rcvy->getEntryBlock() )
        continue;

      new_phi->setIncomingValue(pn, &*i);
    }
  }

  // Find or create a canonical induction variable.
  PHINode *civ = PHINode::Create(u32, 2,"civ", rcvy_header->getFirstNonPHI() );
  Instruction *add = BinaryOperator::CreateNSWAdd(civ, ConstantInt::get(u32,1), "civ.next", rcvy_header->getFirstNonPHI() );

  Function::arg_iterator argit = rcvy->arg_begin();
  Value *low = &*argit;
  ++argit;
  Value *high = &*argit;

  for(pred_iterator i=pred_begin(rcvy_header), e=pred_end(rcvy_header); i!=e; ++i)
  {
    if( rcvy_loop->contains( *i ) )
      civ->addIncoming(add, *i);
    else
      civ->addIncoming(low, *i);
  }

  // Create a new block that represents reaching the
  // high iteration.
  BasicBlock *reachedHigh = BasicBlock::Create(ctx, "reached.high.iteration", rcvy);
  ReturnInst::Create(ctx, ConstantInt::get(u32,0), reachedHigh);

  // Split the header, so it will exit prematurely if
  // the CIV exceeds high.
  BasicBlock *body = rcvy_header->splitBasicBlock( rcvy_header->getFirstNonPHI(), "body.");
  Instruction *cmp = new ICmpInst( CmpInst::ICMP_UGT, civ, high );
  Instruction *br = BranchInst::Create( reachedHigh, body, cmp );
  TerminatorInst *old_term = rcvy_header->getTerminator();

  old_term->eraseFromParent();
  InstInsertPt::End( rcvy_header ) << cmp << br;

  // Finally, delete all the intermediates.
  mloops.forget(rcvy); // very invalid at this point.

  // Cool, we may now erase the cloned function.
  mloops.forget(clone_fcn);
  clone_fcn->eraseFromParent();

  // Erase the loop function.
  mloops.forget(loop_fcn);
  loop_fcn->eraseFromParent();

  // For debugging...
#if 0
  InstInsertPt top = InstInsertPt::End(& rcvy->getEntryBlock());
  std::vector<Value*> args;
  args.push_back( low );
  args.push_back( high );
  insertPrintf(top, "Beginning recovery on iterations %ld through %ld\n", args.begin(), args.end());
  InstInsertPt hd = InstInsertPt::Beginning(rcvy_header);
  insertPrintf(hd, "- Recovery iteration %d\n", civ);

  // For debugging...
  for (inst_iterator ii = inst_begin(rcvy) ; ii != inst_end(rcvy) ; ii++)
  {
    if ( LoadInst* li = dyn_cast<LoadInst>(&*ii) )
    {
      int instr_id = Namer::getInstrId(li);
      if ( instr_id == -1 )
        continue;

      std::ostringstream ss;
      ss << Namer::getInstrId(li);

      Type*        ty = li->getType();
      InstInsertPt pt = InstInsertPt::After(li);

      std::string  fmt1 = " [RECOVERY] * load id " + ss.str() + ", wid 0 iter %d ";
      std::string  fmt2 = " value ";

      if ( ty->isIntegerTy(1) || ty->isIntegerTy(8) || ty->isIntegerTy(16) ||
          ty->isIntegerTy(32) || ty->isIntegerTy(64) )
        fmt2 += "%lld\n";
      else if ( ty->isFloatTy() || ty->isDoubleTy() )
        fmt2 += "%f\n";
      else
        continue;

      insertPrintf(pt, fmt1, civ, false);
      insertPrintf(pt, fmt2, li, true);
    }
    else if ( CmpInst* ci = dyn_cast<CmpInst>(&*ii) )
    {
      int instr_id = Namer::getInstrId(ci);
      if ( instr_id == -1 )
        continue;

      std::ostringstream ss;
      ss << Namer::getInstrId(ci);

      InstInsertPt pt = InstInsertPt::After(ci);

      std::string  fmt1 = " [RECOVERY] * cmp id " + ss.str() + ", wid 0 iter %d ";
      std::string  fmt2 = " value %d\n";

      insertPrintf(pt, fmt1, civ, false);
      insertPrintf(pt, fmt2, ci, true);
    }
  }
#endif

  recovery.fcn = rcvy;
  recovery.liveoutStructure = liveoutStructure;
  return recovery;
}

}
}
