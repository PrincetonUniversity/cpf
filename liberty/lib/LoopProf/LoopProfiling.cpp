#define DEBUG_TYPE "LoopProf"

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

#include "scaf/Utilities/GlobalCtors.h"
#include "scaf/Utilities/InstInsertPt.h"
#include "scaf/Utilities/SplitEdge.h"

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
  class LoopProf : public ModulePass
  {
    bool runOnModule(Module& M);
    bool runOnLoop(Loop *lp);

    int numLoops;

    public:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const
    {
      AU.addRequired<LoopInfoWrapperPass>();
      AU.setPreservesAll();
    }

    StringRef getPassName() const { return "LoopProf"; }
    void *getAdjustedAnalysisPointer(AnalysisID PI) { return this; }

    static char ID;
    LoopProf() : ModulePass(ID) {}
  };
}

char LoopProf::ID = 0;
static RegisterPass<LoopProf> RP10("loop-prof", "Insert loop profiling instrumentation", false, false);


// getFunctionExits - get a set of exit instructions of a function
// count ReturnInst and ResumeInst as exits of a function
// TODO: Does unreachable count as a function exit?
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

// TODO: should this be changed to not profile llvm.dbg.*() calls?
bool LoopProf::runOnLoop(Loop *Lp) {
  BasicBlock *preHeader;
  BasicBlock *header;

  header = Lp->getHeader();
  preHeader = Lp->getLoopPreheader();

  SmallVector<BasicBlock*, 16> exitBlocks;

  Lp->getExitBlocks(exitBlocks);
  Module *M = (header->getParent())->getParent();

  // insert invocation function at end of preheader (called once prior to loop)
  const char* InvocName = "loopProf_invocation";
  FunctionCallee wrapper =  M->getOrInsertFunction(InvocName,
      Type::getVoidTy(M->getContext()), Type::getInt32Ty(M->getContext()));

  Constant *InvocFn = cast<Constant>(wrapper.getCallee());
      //sot
      //Type::getVoidTy(M->getContext()), Type::getInt32Ty(M->getContext()), (Type *)0);
  std::vector<Value*> Args(1);
  Args[0] = ConstantInt::get(Type::getInt32Ty(M->getContext()), numLoops);


  assert(preHeader && "Null preHeader -- Did you run loopsimplify?");

  if (!preHeader->empty())
  {
    CallInst::Create(InvocFn, Args, "", (preHeader->getTerminator()));
  }
  else
  {
    CallInst::Create(InvocFn, Args, "", (preHeader));
  }
  LLVM_DEBUG( errs() << "Loop with preheader " << preHeader->getName() << ": " << numLoops << "\n" );
  LLVM_DEBUG( errs() << "Exit blocks for loop with preheader " << preHeader->getName() << ":\n" );
  for ( auto bb : exitBlocks )
    LLVM_DEBUG( errs() << "\t" << bb->getName() << "\n" );
  LLVM_DEBUG( errs() << *Lp );
  LLVM_DEBUG( errs() << "\n" );

  /*
  // insert iteration begin function at beginning of header (called each loop)
  const char* IterBeginName = "LAMP_loop_iteration_begin";
  Constant *IterBeginFn = M->getOrInsertFunction(IterBeginName, Type::getVoidTy(M->getContext()), (Type *)0);

  // find insertion point (after PHI nodes) -KF 11/18/2008
  for (BasicBlock::iterator ii = header->begin(), ie = header->end(); ii != ie; ++ii) {
  if (!isa<PHINode>(ii)) {
  CallInst::Create(IterBeginFn, "", ii);
  break;
  }
  }

  // insert iteration at cannonical backedge.  exiting block insertions removed in favor of exit block
  const char* IterEndName = "LAMP_loop_iteration_end";
  Constant *IterEndFn = M->getOrInsertFunction(IterEndName, Type::getVoidTy(M->getContext()), (Type *)0);

  // cannonical backedge
  if (!latch->empty())
  CallInst::Create(IterEndFn, "", (latch->getTerminator()));
  else
  CallInst::Create(IterEndFn, "", (latch));
  */

  // insert loop end at beginning of exit blocks
  const char* LoopEndName = "loop_exit";
  FunctionCallee wrapper_end = M->getOrInsertFunction(LoopEndName,
      Type::getVoidTy(M->getContext()), Type::getInt32Ty(M->getContext()));

  Constant *LoopEndFn= cast<Constant>(wrapper_end.getCallee());
      //sot
      //Type::getVoidTy(M->getContext()), Type::getInt32Ty(M->getContext()), (Type *)0);


  set <BasicBlock*> BBSet;
  BBSet.clear();
  for(unsigned int i = 0; i != exitBlocks.size(); i++){
    // this ordering places iteration end before loop exit
    // make sure not inserting the same exit block more than once for a loop -PC 2/5/2009
    if (BBSet.find(exitBlocks[i])!=BBSet.end())
      continue;
    BBSet.insert(exitBlocks[i]);
    BasicBlock::iterator ii = exitBlocks[i]->getFirstInsertionPt();
    exitBlocks[i]->begin();
    //while (isa<PHINode>(ii)) { ii++; }
    //while (isaLAMP(ii)) { ii++; }

    //CallInst::Create(IterEndFn, "", ii);  // iter end placed before exit call

    CallInst::Create(LoopEndFn, Args, "", &*ii);
    //CallInst::Create(LoopEndFn, "", ii);  // loop exiting
  }

  return true;
}


bool LoopProf::runOnModule(Module& M)
{
  numLoops = 0;

  // Go through and instrument each loop,
  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF)
  {
    Function &F = *IF;
    if(F.isDeclaration())
      continue;

    // First collect all call sites in this function, before we add more
    // and mess-up our iterators.
    typedef std::vector<Instruction*> Calls;
    Calls calls;
    for(Function::iterator i=F.begin(), e=F.end(); i!=e; ++i)
      for(BasicBlock::iterator j=i->begin(), z=i->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;

        if( CallBase  *call = dyn_cast<CallBase>(inst) ){
          calls.push_back(inst);
        }
      }

    /* Using the same structures as loops to track time spent in functions
     * This should probably be done a different way
     **/
    ++numLoops;
    const char* InvocName = "loopProf_invocation";
    FunctionCallee wrapper_Invoc =  M.getOrInsertFunction(InvocName,
        Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()));
    Constant *InvocFn = cast<Constant>(wrapper_Invoc.getCallee());
    std::vector<Value*> Args(1);
    Args[0] = ConstantInt::get(Type::getInt32Ty(M.getContext()), numLoops);
    CallInst::Create(InvocFn, Args, "", F.getEntryBlock().getFirstNonPHI() );
    LLVM_DEBUG( errs() << "Function " << IF->getName() << ": " << numLoops << "\n" );

    const char* LoopEndName = "loop_exit";
    FunctionCallee wrapper_LoopEndFn= M.getOrInsertFunction(LoopEndName,
      Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()));
    Constant *LoopEndFn=cast<Constant>(wrapper_LoopEndFn.getCallee());
      //Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()), (Type *)0);
    set <BasicBlock *> BBSet;
    BBSet.clear();

    getFunctionExits(F,BBSet);
    for(set<BasicBlock*>::iterator it = BBSet.begin(), end = BBSet.end();
        it != end; ++it)
    {
      BasicBlock *bb = *it;
      BasicBlock::iterator bbit = bb->end();
      --bbit; //--bbit;
      Instruction *inst = &*bbit;
      CallInst::Create(LoopEndFn, Args, "", inst);
    }

    // Finished inserting calls for function, now handle its loops
    LoopInfo &li = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();

    list<Loop*> loops( li.begin(), li.end() );
    while( !loops.empty() )
    {
      ++numLoops;

      Loop *loop = loops.front();
      loops.pop_front();

      runOnLoop(loop);

      loops.insert( loops.end(),
          loop->getSubLoops().begin(),
          loop->getSubLoops().end());
    }

    // Instrument every callsite within this function
    for(Calls::const_iterator i=calls.begin(), e=calls.end(); i!=e; ++i)
    {
      Instruction *inst = *i;

      ++numLoops;

      // Ziyang: need to ignore all llvm.* instrinsics

      Function *fcn = dyn_cast<CallBase>(inst)->getCalledFunction();
      if (fcn){ // the other case is indirect call
        if (fcn->getName().startswith("llvm."))
          continue;
      }

      Args[0] = ConstantInt::get(Type::getInt32Ty(M.getContext()), numLoops );
      InstInsertPt::Before(inst) << CallInst::Create(InvocFn, Args);

      if( InvokeInst *invoke = dyn_cast<InvokeInst>(inst) )
      {
        // Two successors: normal vs unwind
        // normal
        BasicBlock *upon_normal = split(invoke->getParent(), invoke->getNormalDest(), "after.invoke.normal.");
        InstInsertPt::Beginning(upon_normal) << CallInst::Create(LoopEndFn,Args);

        // Unwind
        BasicBlock *upon_exception = split(invoke->getParent(), invoke->getUnwindDest(), "after.invoke.exception.");
        InstInsertPt::Beginning(upon_exception) << CallInst::Create(LoopEndFn,Args);
        LLVM_DEBUG( errs() << "Invokesite " << *inst << ": " << numLoops << "\n" );
      }
      else
      {
        // CallInst
        InstInsertPt::After(inst) << CallInst::Create(LoopEndFn, Args);
        LLVM_DEBUG( errs() << "Callsite " << *inst << ": " << numLoops << "\n" );
      }

    }
  }

  FunctionCallee wrapper_InitFn =  M.getOrInsertFunction("loopProfInit",
      Type::getVoidTy(M.getContext()),
      Type::getInt32Ty(M.getContext()));

  Constant *InitFn = cast<Constant>(wrapper_InitFn.getCallee());
      //sot
      //(Type *)0);

  std::vector<Value*> Args(1);
  Args[0] = ConstantInt::get(Type::getInt32Ty(M.getContext()), numLoops, false);

  // Create the GlobalCtor function
  std::vector<Type*>FuncTy_0_args;
  FunctionType* FuncTy_0 = FunctionType::get(
      /*Result=*/Type::getVoidTy( M.getContext() ),
      /*Params=*/FuncTy_0_args,
      /*isVarArg=*/false);

  Function* func_lamp_initor = Function::Create(
      /*Type=*/FuncTy_0,
      /*Linkage=*/GlobalValue::ExternalLinkage,
      /*Name=*/"prof_initor", &M);

  BasicBlock *initor_entry = BasicBlock::Create(M.getContext(), "entry", func_lamp_initor,0);
  CallInst::Create(InitFn, Args, "", initor_entry);
  ReturnInst::Create(M.getContext(), initor_entry);

  // Function has been created, now add it to the global ctor list
  callBeforeMain(func_lamp_initor, 0);


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
