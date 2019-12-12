#define DEBUG_TYPE "LAMP"

#include <list>
#include "llvm/IR/Value.h"

// line #
#include "llvm/IR/IntrinsicInst.h"
//#include "llvm/Assembly/Writer.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Analysis/ValueTracking.h"



#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/IR/Module.h"
//#include "llvm/Support/Annotation.h"
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"


#include "llvm/Support/CommandLine.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/CallGraph.h"

#include "llvm/Analysis/BlockFrequencyInfo.h"

#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "typedefs.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include <stdio.h>
#include "liberty/Utilities/LiveValues.h"
//#include "SimpleProfReader.h"

#include "LAMPLoopSample.h"

#define THREADNUM 8

namespace liberty {

  using namespace llvm;
  using namespace std;

  cl::opt<bool> INVOCATION(
      "lamp-invocation-sample", cl::init(false), cl::NotHidden,
      cl::desc("Sample invocations rather than iterations"));

  cl::opt<int> RATE(
      "lamp-sample-rate", cl::init(256), cl::NotHidden,
      cl::desc("Set rate for sampling"));

  cl::opt<int> OFFSET (
      "lamp-sample-offset", cl::init(0), cl::NotHidden,
      cl::desc("Set offset for (parallel) sampling"));

  cl::opt<int> CONSEC(
      "lamp-sample-consec", cl::init(100), cl::NotHidden,
      cl::desc("Set consecutive iterations for sampling"));

  cl::opt<int> CONSEC_OFF(
      "lamp-sample-consec-off", cl::init(-1), cl::NotHidden,
      cl::desc("Set consecutive iterations off for sampling"));


  typedef std::list<Instruction *> intList;
  typedef std::list<Instruction *>::iterator intListIter;

  void LAMPLoopSample::getAnalysisUsage(AnalysisUsage &AU) const
  {
    //AU.addRequired< SimpleProfReader >();
    AU.addRequired< BlockFrequencyInfoWrapperPass >();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
  }


  char LAMPLoopSample::ID = 0;

  namespace
  {
    static RegisterPass<LAMPLoopSample> RP ("LAMPLoopSample",
        "Create clone version of loop, remove lamp sampling from cloned version",
        false, false);
  }

  LAMPLoopSample::LAMPLoopSample() : ModulePass(ID)
  {
    if(CONSEC_OFF == -1)
    {
      int consec_v = CONSEC;
      CONSEC_OFF = consec_v;
    }
  }

  LAMPLoopSample::~LAMPLoopSample() {}

  void LAMPLoopSample::reset()
  {
    funcId = 0;
  }


  /* Delete call instructions */
  void LAMPLoopSample::removeLampCalls(intList &delList)
  {
    for(intListIter it = delList.begin(), lend = delList.end();
        it != lend; ++it)
    {
      Instruction *inst = *it;

      inst->dropAllReferences();
      inst->removeFromParent();
    }
  }

  /* Given a single instruction, replace the call with the proper
   * call to the __nolamp_ version
   */
  void LAMPLoopSample::replaceCall(Module &M, Instruction *inst)
  {
    CallSite call = CallSite(inst);
    Function *f = call.getCalledFunction();

    // If this is a function we have source for, change call to __nolamp_ version
    if(!f->isDeclaration() && !f->isIntrinsic() && !f->hasAvailableExternallyLinkage() )
    {
      Function *clone = funcMap[f];
      call.setCalledFunction(clone);
    }
  }

  /* Given a list of call instructions, replace all of the calls with the
   * __nolamp_ version of the call
   */
  void LAMPLoopSample::replaceCalls(Module &M, intList &callList)
  {
    for(intListIter it = callList.begin(), lend = callList.end();
        it != lend; ++it)
    {
      Instruction *inst = *it;
      replaceCall(M, inst);
    }
  }


  void LAMPLoopSample::sanitizeLoop(Loop *loop, Module &M)
  {
    intList delList;

    for(Loop::block_iterator bb = loop->block_begin(), bbe = loop->block_end(); bb != bbe; ++bb)
    {
      BasicBlock *BBB = *bb;
      for(BasicBlock::iterator IB = BBB->begin(), IE = BBB->end(); IB != IE; IB++)
      {
        if(isa<CallInst>(IB) || isa<InvokeInst>(IB))
        {
          CallSite call = CallSite(&*IB);
          Function *f = call.getCalledFunction();
          if(f == NULL || f->hasAvailableExternallyLinkage())
          {
            // This is an indirect call, we can't do anything
            continue;
          }

          std::string cname = f->getName();

          // Calls to LAMP that need to be removed
          if( cname.find("LAMP") != std::string::npos )
          {
            if(cname.find("LAMP_loop_invocation") == std::string::npos &&
                cname.find("LAMP_loop_exit") == std::string::npos)
            {
              /*
                 LLVM_LLVM_DEBUG( errs() << "~~~Removing " << cname << " from the cloned loop "
                 << loop->getHeader()->getName() << " in "
                 << call.getCaller()->getName() << "\n");
                 */
              delList.push_front(&*IB);
            }
          }
          else
          {
            replaceCall(M, &*IB);
          }
        }
      }
    }

    removeLampCalls(delList);
  }

  /* Create copies of the functions of the program with no lamp calls
   * ensure these functions only call other sanitized functions
   */
  void LAMPLoopSample::sanitizeFunctions(Module &M)
  {
    intList delList;
    intList callList;

    // Need to clone each function, remove lamp calls (leave iteration/invocation calls?)
    // and re-name the function so that the non-prof loops can call into clean functions
    for(Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    {
      if(!I->isDeclaration())// && !I->hasAvailableExternallyLinkage() )
      {
        Function *orig = &*I;

        std::string nstr = orig->getName();
        if(nstr.find("__nolamp_") != std::string::npos || nstr == "main")
        {
          // We have already modified this function, ignore
          continue;
        }
        //LLVM_LLVM_DEBUG(errs() << "~Considering function " << nstr << "\n");

        ValueToValueMapTy VMap;
        Function *clone = CloneFunction(orig, VMap);
        assert(clone->hasName() && "Function did not have name");
        clone->setName("__nolamp_" + orig->getName());
        // sot: CloneFunction in LLVM 5.0 inserts the cloned function in the function's module
        //I->getParent()->getFunctionList().push_back(clone);
        //LLVM_LLVM_DEBUG(errs() << "~~Created cloned version " << clone->getName() << "\n");

        funcMap[orig] = clone;

        // Duplicate is created, now remove all lamp calls from the function
        // and change all function calls to their non-lamp versions
        for(Function::iterator BBB = clone->begin(), BBE = clone->end(); BBB != BBE; ++BBB)
        {
          for(BasicBlock::iterator IB = BBB->begin(), IE = BBB->end(); IB != IE; IB++)
          {
            if(isa<CallInst>(IB) || isa<InvokeInst>(IB))
            {
              CallSite call = CallSite(&*IB);
              Function *f = call.getCalledFunction();
              if(f == NULL || f->hasAvailableExternallyLinkage())
              {
                // indirect call, can't fix it
                continue;
              }
              std::string cname = f->getName();

              // Calls to LAMP that need to be removed
              if( cname.find("LAMP") != std::string::npos )
              {
                //LLVM_LLVM_DEBUG( errs() << "~~~Removing " << cname << " from the function "
                //              << clone->getName() << "\n");

                delList.push_front(&*IB);
              }
              else
              {
                callList.push_front(&*IB);
              }
            }
          }
        }

      }
    }

    removeLampCalls(delList);
    replaceCalls(M, callList);
  }



  /* Clone the loop and return the clone
   * Most of this code comes from DOALLCloneLoop
   */
  Loop* LAMPLoopSample::LAMPCloneLoop(Loop *OrigL, LoopInfo *LI,
      ValueToValueMapTy &clonedValueMap,
      SmallVector<BasicBlock *, 16> &ClonedBlocks)
  {
    // Create New Loop structure
    Loop *NewLoop = new Loop();

    // Clone Basic Blocks.
    for (Loop::block_iterator I = OrigL->block_begin(), E = OrigL->block_end();
        I != E; ++I)
    {
      BasicBlock *BB = *I;
      BasicBlock *NewBB = CloneBasicBlock(BB, clonedValueMap, ".clone");
      clonedValueMap[BB] = NewBB;

      for (BasicBlock::iterator ii = NewBB->begin(), ee = NewBB->end();
          ii != ee; ++ii)
      {
        //LLVM_LLVM_DEBUG(errs() << *ii << "\n");
      }
      /// Remember clonedHeader and clonedLoopExitingBB

      /*
         if (BB == Header)
         clonedHeader = NewBB;
         if (BB == loopExitingBB)
         clonedLoopExitingBB = NewBB;
         */
      LoopInfoBase<BasicBlock, Loop> &LIB = *LI;
      NewLoop->addBasicBlockToLoop(NewBB, LIB);
      ClonedBlocks.push_back(NewBB);
    }


    // Remap instructions to reference operands from ValueMap.
    for(SmallVector<BasicBlock *, 16>::iterator NBItr = ClonedBlocks.begin(),
        NBE = ClonedBlocks.end();  NBItr != NBE; ++NBItr)
    {
      BasicBlock *NB = *NBItr;
      for(BasicBlock::iterator BI = NB->begin(), BE = NB->end();
          BI != BE; ++BI)
      {
        Instruction *Insn = &*BI;
        for (unsigned index = 0, num_ops = Insn->getNumOperands();
            index != num_ops; ++index)
        {
          Value *Op = Insn->getOperand(index);
          ValueToValueMapTy::iterator OpItr = clonedValueMap.find(Op);
          if (OpItr != clonedValueMap.end())
            Insn->setOperand(index, OpItr->second);
        }
      }
    }

    BasicBlock *Latch = OrigL->getLoopLatch();
    Function *F = Latch->getParent();
    BasicBlock **ClonedBlBegin = &*(ClonedBlocks.begin());
    BasicBlock **ClonedBlEnd = &*(ClonedBlocks.end());
    llvm::iplist_impl<llvm::simple_ilist<llvm::BasicBlock>, llvm::SymbolTableListTraits<llvm::BasicBlock>>::iterator it(OrigL->getLoopPreheader());
    F->getBasicBlockList().insert(it,
        ClonedBlBegin, ClonedBlEnd);

    return NewLoop;
  }


  void LAMPLoopSample::fixBackEdge(BasicBlock *latch, BasicBlock *header,
      BasicBlock *branchBlock)
  {
    Instruction *jmp = latch->getTerminator();
    unsigned numSucc = jmp->getNumSuccessors();
    for(unsigned i=0; i<numSucc; ++i)
    {
      if(jmp->getSuccessor(i) == header)
      {
        jmp->setSuccessor(i, branchBlock);
      }
    }
  }


  /* ENTRY POINT
   * Do the lampsample transfomation
   */
  bool LAMPLoopSample::runOnModule(Module &M)
  {
    LLVM_LLVM_DEBUG(errs() << "\n\nEntering LAMPLoopSampler\n");


    //CallGraph *cg = getAnalysisIfAvailable< CallGraph >();
    //TripCounts *tc = TripCounts::getTripCounts(M, cg);
    //ProfileInfo &PI = getAnalysis<ProfileInfo>();
    //SimpleProfReader &PR = getAnalysis<SimpleProfReader>();

    LLVM_LLVM_DEBUG(errs() << "Sanitizing functions\n");
    sanitizeFunctions(M);

    /*
       LLVM_LLVM_DEBUG(
       errs() << "\n\n";
       for(Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
       if(!I->isDeclaration())
       {
       Function *orig = I;
       Function *clone = funcMap[orig];
       if(clone == NULL)
       errs() << orig->getName() << " Clone was null\n";
       else
       errs() << "-Function name: " << orig->getName() << " to " << clone->getName() << "\n";
       });
       */


    LLVM_LLVM_DEBUG(errs() << "Cloning the loops\n");
    // Create the cloned loops
    for(Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
      if(!I->isDeclaration())
      {
        std::string nstr = I->getName();
        if(nstr.find("__nolamp_") != std::string::npos)
        {
          // This is a no-prof function, ignore it
          continue;
        }

        Function *F = &*I;
        LiveValues lva(*F,false);

        // Now have the function, need to go over each of it's loops and samplify them
        LoopInfo  &li = getAnalysis<LoopInfoWrapperPass>(*F).getLoopInfo();
        std::vector<Loop *> loops(li.begin(), li.end());

        BlockFrequencyInfo &bfi = getAnalysis< BlockFrequencyInfoWrapperPass >(*F).getBFI();

        while(!loops.empty())
        {
          Loop *loop = loops.back();
          loops.pop_back();

          loops.insert(loops.end(),
              loop->getSubLoops().begin(),
              loop->getSubLoops().end());

          // make the branch block
          BasicBlock *preHeader = loop->getLoopPreheader();
          BasicBlock *header = loop->getHeader();

          // make the counter
          GlobalVariable *cnt = new GlobalVariable(M,
              IntegerType::get(M.getContext(), 64),
              false,
              GlobalValue::ExternalLinkage,
              0,
              "");
          cnt->setAlignment(8);

          //unsigned long loopIter = (unsigned long)tc->count(loop);
          //unsigned long loopIter = (unsigned long)PI.getExecutionCount(header);
          //PI.repair(F);
          //double loopIter = PI.getExecutionCount(loop->getLoopLatch());
          //double loopIter = PR.count(F->getName().str() + header->getName().str());

          if( bfi.getBlockProfileCount(header).hasValue() )
          {
            double loopIter = bfi.getBlockProfileCount(header).getValue();
            errs() << F->getName() << " " << header->getName() << " has count " << loopIter;
            CONSEC = loopIter/RATE;
            if(CONSEC < 2)
            {
              errs() << " --Consec too low, bumping up to 2";
              CONSEC = 2;
            }
#define CAP 10000
            else if(CONSEC > CAP)
            {
              errs() << " --Consec too high, capping at " << CAP;
              CONSEC = CAP;
            }
            int consec_v = CONSEC;
            CONSEC_OFF = consec_v;
            //RATE = THREADNUM;
            errs() << "\n";
          }
          else
          {
            errs() << "In the wrong place\n";
          }


          if(OFFSET==0)
          {
            cnt->setInitializer(ConstantInt::get(M.getContext(),
                  APInt(64, 0 )));
          }
          else
          {
            cnt->setInitializer(ConstantInt::get(M.getContext(),
                  APInt(64, (CONSEC*RATE) - (OFFSET*CONSEC) )));
          }

          ConstantInt* const_int64_1 = ConstantInt::get(Type::getInt64Ty(M.getContext()), 1);
          ConstantInt* const_int64_consec = ConstantInt::get(
              Type::getInt64Ty(M.getContext()), CONSEC);
          ConstantInt* const_int64_consec_rate = ConstantInt::get(
              Type::getInt64Ty(M.getContext()), CONSEC_OFF*RATE);

          if(!INVOCATION)
          {
            LoopInfo  *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
            DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass >().getDomTree();
            BasicBlock *branchBlock = SplitEdge(preHeader, header, DT, LI);

            // clone the loop and remove LAMPload/store calls
            ValueToValueMapTy clonedValueMap;
            SmallVector<BasicBlock *, 16> ClonedBlocks;
            Loop *clonedLoop = LAMPCloneLoop(loop, &li, clonedValueMap, ClonedBlocks);
            BasicBlock *cloneHeader = clonedLoop->getHeader();

#if 0
            // Fix PHI Nodes
            InstInsertPt splitEntry = InstInsertPt::Beginning(branchBlock);

            std::vector<PHINode *> PHIvector;
            PHINode *PN;

            for(BasicBlock::iterator I = header->begin();
                (PN = dyn_cast<PHINode>(I)) != NULL;
                ++I)
            {
              // Keep the phis around to delete later
              PHIvector.push_back(PN);
              PHIvector.push_back(cast<PHINode>(clonedValueMap[PN]));

              // Create the new one and insert it into the top of branchBlock
              PHINode *newPN = (PHINode *)PN->clone();
              splitEntry << newPN;

              unsigned num = PN->getNumIncomingValues();
              for(unsigned index = 0; index < num; ++index)
              {
                BasicBlock *bb = PN->getIncomingBlock(index);
                if(bb == branchBlock)
                {
                  newPN->setIncomingBlock(index, preHeader);
                }
                else
                {
                  Value *V = clonedValueMap[PN->getIncomingValue(index)];
                  if(V == NULL)
                  {
                    V = PN->getIncomingValue(index);
                  }
                  BasicBlock *bb = cast<BasicBlock>(clonedValueMap[PN->getIncomingBlock(index)]);
                  newPN->addIncoming(V, bb);
                }
              }
              Value *clonedVal = clonedValueMap[PN];
              clonedVal->replaceAllUsesWith(newPN);
              PN->replaceAllUsesWith(newPN);
            }

            for(std::vector<PHINode *>::iterator I = PHIvector.begin(), IE = PHIvector.end();
                I != IE;
                ++I)
            {
              PHINode *node = *I;
              node->eraseFromParent();
            }

            // Fix more PHI nodes that are live out from loop exiting blocks to exit blocks
            SmallVector<BasicBlock *, 8> exitingBlocks, exitBlocks;
            loop->getExitingBlocks(exitingBlocks);
            loop->getExitBlocks(exitBlocks);

            for(SmallVector<BasicBlock *, 8>::iterator ebb = exitingBlocks.begin(), end = exitingBlocks.end();
                ebb != end; ++ebb)
            {
              vector<const Value*> liveOuts;
              const BasicBlock *exitingBlock = *ebb;
              lva.findLiveOutFromBB(exitingBlock,liveOuts);

              const TerminatorInst *term = exitingBlock->getTerminator();
              unsigned numSucc = term->getNumSuccessors();
              for(int ii = 0; ii < numSucc; ++ii)
              {
                BasicBlock *succBlock = term->getScuccessor(ii);
                InstInsertPt succEntry = InstInsertPt::Beginning(succBlock);
              }
            }
#endif

            // Phi nodes are fixed, remove all lamp calls from clonedLoop
            sanitizeLoop(clonedLoop, M);

            // Increment the variable
            Instruction *brB = &*(--(branchBlock->end()));
            LoadInst *cnt_tmp = new LoadInst(cnt, "", false, brB);
            BinaryOperator *cnt_inc = BinaryOperator::Create(Instruction::Add, cnt_tmp,
                const_int64_1,
                "inc", brB);
            new StoreInst(cnt_inc, cnt, false, brB);
            ICmpInst* cmp1 = new ICmpInst(brB, ICmpInst::ICMP_SLT, cnt_tmp,
                const_int64_consec, "cmp1");
            BranchInst::Create(header, cloneHeader, cmp1, brB);

            // This block correctly branches, remove old fake branch
            brB->dropAllReferences();
            brB->removeFromParent();


            Instruction *fst = cloneHeader->getFirstNonPHI();
            cnt_tmp = new LoadInst(cnt, "", false, fst);
            BinaryOperator* cnt_rem = BinaryOperator::Create(Instruction::URem,
                cnt_tmp, const_int64_consec_rate, "rem", fst);
            new StoreInst(cnt_rem, cnt, false, fst);


            // make back edges go to if block -- loop simplify means 1 back edge
            BasicBlock *latch = loop->getLoopLatch();
            BasicBlock *cloneLatch = clonedLoop->getLoopLatch();
            assert( (loop->getNumBackEdges() == 1) && "Should be only 1 back edge, loop-simplify");
            assert ( latch != 0 && "Loop latch needs to exist, loop-simplify?" );

            fixBackEdge(latch, header, branchBlock);
            fixBackEdge(cloneLatch, cloneHeader, branchBlock);

            preHeader->moveBefore(cloneHeader);
            branchBlock->moveBefore(cloneHeader);
          }
          else
          {
            // clone the loop and remove LAMPload/store calls
            ValueToValueMapTy clonedValueMap;
            SmallVector<BasicBlock *, 16> ClonedBlocks;
            Loop *clonedLoop = LAMPCloneLoop(loop, &li, clonedValueMap, ClonedBlocks);
            sanitizeLoop(clonedLoop, M);
            BasicBlock *cloneHeader = clonedLoop->getHeader();


            // Add to the end of the header block
            Instruction *brB = &*(--(preHeader->end()));

            // If the counter has rolled around, reset it
            LoadInst *cnt_tmp = new LoadInst(cnt, "", false, brB);
            BinaryOperator* cnt_rem = BinaryOperator::Create(Instruction::URem,
                cnt_tmp, const_int64_consec_rate, "rem", brB);


            // Increment the variable
            BinaryOperator *cnt_inc = BinaryOperator::Create(Instruction::Add, cnt_rem,
                const_int64_1,
                "inc", brB);
            new StoreInst(cnt_inc, cnt, false, brB);
            ICmpInst* cmp1 = new ICmpInst(brB, ICmpInst::ICMP_SLT, cnt_tmp,
                const_int64_consec, "cmp1");
            BranchInst::Create(header, cloneHeader, cmp1, brB);
            // This block correctly branches, remove old fake branch
            brB->dropAllReferences();
            brB->removeFromParent();

            preHeader->moveBefore(cloneHeader);
          }
        }

      }

    return false;
  }
}

#undef DEBUG_TYPE
