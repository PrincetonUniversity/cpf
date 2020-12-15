#define DEBUG_TYPE "SpiceProf"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"

#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/SplitEdge.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/LoopProf/LoopProfLoad.h"

#include <iostream>
#include <fstream>
#include <set>
#include <sstream>
#include <list>

using namespace llvm;
using namespace std;
using namespace liberty;

namespace
{
  class SpiceProf : public ModulePass
  {
    bool runOnModule(Module& M);
    bool runOnLoop(Loop *lp);

    int numLoops;

    public:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const
    {
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addRequired<Targets>();
      AU.addRequired<LoopProfLoad>();
      AU.setPreservesAll();
    }

    StringRef getPassName() const { return "SpiceProf"; }
    void *getAdjustedAnalysisPointer(AnalysisID PI) { return this; }

    static char ID;
    SpiceProf() : ModulePass(ID) {}
  };
}

char SpiceProf::ID = 0;
static RegisterPass<SpiceProf> RP10("spice-prof", "Insert spice profiling instrumentation", false, false);


void getFunctionExits(Function &F,set<BasicBlock*> &bbSet)
{
  for(Function::iterator bbit = F.begin(), end = F.end();
      bbit != end; ++bbit)
  {
    BasicBlock *bb = &*bbit;
    if( isa<ReturnInst>(bb->getTerminator())
    ||  isa<ResumeInst>(bb->getTerminator()) )
    {
      bbSet.insert(bb);
    }
  }
}

bool SpiceProf::runOnLoop(Loop *Lp) {
  assert(Lp->isLoopSimplifyForm() && "did not run loop simplify\n");

  //get preheader, header, Module, exit blocks
  BasicBlock *preHeader = Lp->getLoopPreheader();
  BasicBlock *header = Lp->getHeader();
  Module *M = (header->getParent())->getParent();
  SmallVector<BasicBlock*, 16> exitBlocks;
  Lp->getExitBlocks(exitBlocks);

  //debugs: print out preheader and exitblocks
  LLVM_DEBUG( errs() << "Loop with preheader " << preHeader->getName() << ": " << numLoops << "\n" );
  LLVM_DEBUG( errs() << "Exit blocks for loop with preheader " << preHeader->getName() << ":\n" );
  for ( auto bb : exitBlocks )
    LLVM_DEBUG( errs() << "\t" << bb->getName() << "\n" );
  LLVM_DEBUG( errs() << *Lp );
  LLVM_DEBUG( errs() << "\n" );

  //get all the PHINodes in header
  SmallVector<PHINode*, 16> headerPHIs;
  for (BasicBlock::iterator ii = header->begin(), ie = header->end(); ii != ie; ++ii) {
    if (isa<PHINode>(ii)) {
      headerPHIs.push_back((PHINode*) &*ii);
      LLVM_DEBUG( errs() << "inst: " << *ii << " is a phi\n");
    }
    else break;
  }

  //create function type for void spice_profile_load_ptr(void* ptr, int staticNum)
  std::vector<Type*>FuncTy_load_ptr_args(2);
  FuncTy_load_ptr_args[0]=Type::getInt8PtrTy(M->getContext()); /*1st arg: bitcast type*/
  FuncTy_load_ptr_args[1]=Type::getInt32Ty(M->getContext()); /*2nd arg: int*/

  FunctionType* FuncTy_profile_load_ptr_ty = FunctionType::get(
      /*Result=*/Type::getVoidTy(M->getContext()),
      /*Params=*/FuncTy_load_ptr_args,
      /*isVarArg=*/false);
  FunctionCallee wrapper_ProfileLdPtrFn = M->getOrInsertFunction("__spice_profile_load_ptr", FuncTy_profile_load_ptr_ty);
  Constant *ProfileLdPtrFn = cast<Constant>(wrapper_ProfileLdPtrFn.getCallee());


  //create function type for void spice_profile_load_double(double ptr, int staticNum)
  std::vector<Type*>FuncTy_load_double_args(2);
  FuncTy_load_double_args[0]=Type::getDoubleTy(M->getContext());
  FuncTy_load_double_args[1]=Type::getInt32Ty(M->getContext()); /*2nd arg: int*/

  FunctionType* FuncTy_profile_load_double_ty = FunctionType::get(
      /*Result=*/Type::getVoidTy(M->getContext()),
      /*Params=*/FuncTy_load_double_args,
      /*isVarArg=*/false);
  FunctionCallee wrapper_ProfileLdDbFn = M->getOrInsertFunction("__spice_profile_load_double", FuncTy_profile_load_double_ty);
  Constant *ProfileLdDbFn = cast<Constant>(wrapper_ProfileLdDbFn.getCallee());

  // insert bit cast of headerphis in the successor of header and call profile_load
  BasicBlock* latch = Lp->getLoopLatch();
  for(BasicBlock::iterator ii = latch->begin(), ie = latch->end(); ii != ie; ++ii){
    if( !isa<PHINode>(ii) ){

      SmallVector<Instruction*, 16> phiPtrCasts;
      SmallVector<Instruction*, 16> phiDoubles;
      //insert bitcast
      for(auto &phi : headerPHIs){
        Type* phiType = phi->getType();
        const Twine bitcastName = "scast." + phi->getName();
        if(!phiType->isDoubleTy()){
          Type* int8ptrT = Type::getInt8PtrTy(M->getContext());
          CastInst* phiCast = CastInst::CreateBitOrPointerCast(phi, int8ptrT, bitcastName, &*ii);
          phiPtrCasts.push_back(phiCast);
        }
        else{
          phiDoubles.push_back(phi);
        }
      }

      int phiCnt = 0;
      std::vector<Value*> Args(2);
      for(auto &phiCast : phiPtrCasts){
        Args[0] = phiCast;
        Args[1] = ConstantInt::get(Type::getInt32Ty(M->getContext()), phiCnt);
        CallInst::Create(ProfileLdPtrFn, Args, "", &*ii);
        phiCnt++;
      }

      for(auto &phiDouble : phiDoubles){
        Args[0] = phiDouble;
        Args[1] = ConstantInt::get(Type::getInt32Ty(M->getContext()), phiCnt);
        CallInst::Create(ProfileLdDbFn, Args, "", &*ii);
        phiCnt++;
      }
      break;
    }
  }

  // insert spice invocation function at end of preheader (called once prior to loop)
  const char* InvocName = "__spice_start_invocation";
  FunctionCallee wrapper =  M->getOrInsertFunction(InvocName,
      Type::getVoidTy(M->getContext()), Type::getInt32Ty(M->getContext()));
  Constant *InvocFn = cast<Constant>(wrapper.getCallee());
  std::vector<Value*> Args(1);
  Args[0] = ConstantInt::get(Type::getInt32Ty(M->getContext()), numLoops);
  //assert(preHeader && "Null preHeader -- Did you run loopsimplify?");
  if (!preHeader->empty())
    CallInst::Create(InvocFn, Args, "", (preHeader->getTerminator()));
  else
    CallInst::Create(InvocFn, Args, "", (preHeader));




  // insert end invocation at beginning of exit blocks
  std::vector<Type*>FuncTy_0_args;
  FunctionType* FuncTy_void_void = FunctionType::get(
      /*Result=*/Type::getVoidTy(M->getContext()),
      /*Params=*/FuncTy_0_args,
      /*isVarArg=*/false);

  FunctionCallee wrapper_LoopEndFn = M->getOrInsertFunction("__spice_end_invocation", FuncTy_void_void);
  Constant *LoopEndFn = cast<Constant>(wrapper_LoopEndFn.getCallee());

  set <BasicBlock*> BBSet;
  BBSet.clear();
  for(unsigned int i = 0; i != exitBlocks.size(); i++){
    if (BBSet.find(exitBlocks[i])!=BBSet.end())
      continue;
    BBSet.insert(exitBlocks[i]);
    BasicBlock::iterator ii = exitBlocks[i]->getFirstInsertionPt();
    while (isa<PHINode>(ii)) { ii++; }
    CallInst::Create(LoopEndFn, "", &*ii);
  }

  return true;
}


bool SpiceProf::runOnModule(Module& M)
{
  // get required loop prof
  LoopProfLoad &load = getAnalysis< LoopProfLoad >();
  Targets &tg = getAnalysis<Targets>();

  for(Targets::header_iterator i=tg.begin(), e=tg.end(); i!=e; ++i)
  {
    BasicBlock *header = *i;
    Function *fcn = header->getParent();

    char percent[10];
    const unsigned long loop_time = load.getLoopTime(header);
    snprintf(percent,10, "%.1f", 100.0 * loop_time / load.getTotTime());

    errs() << " - " << fcn->getName() << " :: " << header->getName();
    Instruction *term = header->getTerminator();
    if (term)
      liberty::printInstDebugInfo(term);
    errs() << "\tTime " << loop_time << " / " << load.getTotTime()
           << " Coverage: " << percent << "%\n";
  }

  numLoops = 0;
  std::set<BasicBlock*> ExitBBs;

  // Go through and instrument each loop,
  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF)
  {
    Function &F = *IF;
    if(F.isDeclaration())
      continue;

    // Finished inserting calls for function, now handle its loops
    LoopInfo &li = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();

    //get a list of highest level loops
    list<Loop*> loops( li.begin(), li.end() );
    while( !loops.empty() )
    {

      Loop *loop = loops.front();
      loops.pop_front();

      runOnLoop(loop);

      loops.insert( loops.end(),
          loop->getSubLoops().begin(),
          loop->getSubLoops().end());

      ++numLoops; // calls start with integer 0
    }

    ExitBBs.clear();
    getFunctionExits(F,ExitBBs);
  }

  //create the printall function
  std::vector<Type*>FuncTy_0_args;
  FunctionType* FuncTy_void_void = FunctionType::get(
      /*Result=*/Type::getVoidTy( M.getContext() ),
      /*Params=*/FuncTy_0_args,
      /*isVarArg=*/false);

  Function* func_beforeMain = Function::Create(
      /*FunctionType=*/FuncTy_void_void,
      /*Linkage=*/GlobalValue::ExternalLinkage,
      /*Name=*/"__spice_before_main", &M);


  //insert printall at every exit
  //for(auto const &bb : ExitBBs){
    //for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
      //if (isa<ReturnInst>(ii)) {
        //LLVM_DEBUG( errs() << "inst: " << *ii << " is a returnInst\n");
        //CallInst::Create(func_printall, "", &*ii);
      //}
    //}
  //}
  callBeforeMain(func_beforeMain);

  return false;
}






#if 0

bool isaLAMP(Instruction *inst)
{
  if(isa<CallInst>(inst) || isa<InvokeInst>(inst))
  {
    CallSite call = CallSite(inst);
    Function *f = call.getCalledFunction();
    if( f != NULL)
    {
      std::string cname = f->getNameStr();
      if( cname.find("LAMP") != std::string::npos)
      {
        return true;
      }
    }
  }
  return false;
}


#endif

#undef DEBUG_TYPE
