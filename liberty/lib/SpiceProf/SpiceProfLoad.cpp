#define DEBUG_TYPE "SpiceProfLoad"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/CallSite.h"

#include "llvm/ADT/Statistic.h"
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
#include "liberty/LoopProf/Targets.h"

#include "liberty/Utilities/GlobalCtors.h"

#include <iostream>
#include <fstream>
#include <set>
#include <sstream>
#include <list>

#include "liberty/SpiceProf/SpiceProfLoad.h"

namespace liberty
{
using namespace llvm;
using namespace std;
using namespace liberty;

//STATISTIC(numNonZero, "XXX Loops with non-zero execution weight");

char SpiceProfLoad::ID = 0;

//const char* fname = "LoopTimes.out";

static RegisterPass<SpiceProfLoad> RP10("spice-prof-load",
    "(SpiceProfLoad) Load spice profiling information", false, false);

static cl::opt<bool> profDump(
    "spice-prof-dump",
    cl::init(false),
    cl::NotHidden,
    cl::desc("dump spice profiling information"));


void SpiceProfLoad::profile_dump(void)
{
  ofstream f;
  f.open(SPICE_OUT_FILE);
  f.close();
}


bool SpiceProfLoad::runOnLoop(Loop *Lp)
{
  assert(Lp->isLoopSimplifyForm() && "did not run loop simplify\n");
  BasicBlock *header = Lp->getHeader();
  int phiCnt = 0;
  for (BasicBlock::iterator ii = header->begin(), ie = header->end(); ii != ie; ++ii) {
    Instruction* inst = &*ii;
    if (isa<PHINode>(ii)) {
      phiToloopNum.insert({inst, numLoops});
      phiTovarNum.insert({inst, phiCnt});
      phiCnt++;
    }
  }
  return true;
}

double SpiceProfLoad::compare_loop_invokes(vals_t curr, vals_t prev)
{
  int totalPrevVals = prev.size();
  int matched = 0;
  int jStart = 0;
  for(int i=0; i<totalPrevVals; i++)
  {
    for(int j=jStart; j<curr.size(); j++)
    {
      if(curr[i] == prev[j]){
        matched ++;
        jStart = j+1;
        break;
      }
    }
  }
  return (double) matched / totalPrevVals;
}

void SpiceProfLoad::delete_loop_from_phiMap(int loopNum)
{
  vector<Instruction*> inst2delete;
  for(auto& x : phiToloopNum)
    if(x.second == loopNum)
      inst2delete.push_back(x.first);
  for(auto& inst : inst2delete)
  {
    phiToloopNum.erase(inst);
    phiTovarNum.erase(inst);
  }
}

void SpiceProfLoad::__spice_calculate_similarity()
{
  for(auto &x : per_loop_vals)
  {
    int loop_name = x.first;
    cout << "********loop num: " << loop_name << "**********\n";

    LoopVal_t loop_vals = *(x.second);
    int invoke_count = loop_vals.size();
    cout << "loop recorded invoke count = " << invoke_count << "\n";
    if(invoke_count < 2)
    {
      cout << "loop invoked less than the threshold * 2 and is not considered\n";
      delete_loop_from_phiMap(loop_name);
      continue;
    }
    InvokeVal_t prev_invocation;
    bool isNotFirstRec = false;

    similarity_t* sim = new similarity_t();
    for(auto &loop_invoke : loop_vals)
    {
      int j = loop_invoke.first;
      InvokeVal_t curr_invoke = *(loop_vals[j]);
      for(auto &y : curr_invoke)
      {
        int varId = y.first;
        if(!sim->count(varId))
          sim->insert({varId, 0.0});

        double similarity = (*sim)[varId];
        if(isNotFirstRec)
        {
          vals_t curr_vals = *(y.second);
          vals_t prev_vals = *(prev_invocation[varId]);
          similarity += compare_loop_invokes(curr_vals, prev_vals);
        }
        (*sim)[varId] = similarity;
      }
      //similarity / invoke count
      prev_invocation = curr_invoke;
      isNotFirstRec = true;
    }

    for(auto& s : *sim)
    {
      double sLevel = s.second / (invoke_count - 1);
      cout << "similarity for var " << s.first << " is " << sLevel << "\n";
      (*sim)[s.first] = sLevel;
    }
    similarities.insert({loop_name, sim});
  }
}

void SpiceProfLoad::__spice_read_profile()
{

  ifstream rf("spice.prof", ios::in | ios::binary);
  int curr_invoke_count = -1;
  while(!rf.eof())
  {
    //read loop
    int loop_name;
    rf.read((char*)&loop_name, sizeof(int));

    //read invoke count
    int invoke_count;
    rf.read((char*)&invoke_count, sizeof(int));

    //read static var count
    int varNum;
    rf.read((char*)&varNum, sizeof(int));

    //read ptr
    void* ptr;
    rf.read((char*)&ptr, sizeof(void*));

    //create a loop profile if not created
    if(!per_loop_vals.count(loop_name))
      per_loop_vals.insert({loop_name, new LoopVal_t()});
    LoopVal_t* curr_loop_vals = per_loop_vals[loop_name];

    //create a new invoke if not create
    if(!curr_loop_vals->count(invoke_count))
      curr_loop_vals->insert({invoke_count,new InvokeVal_t()});
    InvokeVal_t* curr_invoke_vals = (*curr_loop_vals)[invoke_count];
    curr_invoke_count = invoke_count;

    //create a new variable if not create
    if(!curr_invoke_vals->count(varNum))
      curr_invoke_vals->insert({varNum, new vals_t()});
    vals_t* curr_vals = (*curr_invoke_vals)[varNum];
    curr_vals->push_back((long)ptr);
  }

}

bool SpiceProfLoad::isTargetLoop(Loop* l)
{
  // get required loop prof
  Targets &tg = getAnalysis<Targets>();

  for(Targets::header_iterator i=tg.begin(), e=tg.end(); i!=e; ++i)
  {
    BasicBlock *header = *i;
    Function *fcn = header->getParent();

    if(header == l->getHeader())
    {
      errs() << "loop num: " << numLoops << "\n";
      errs() << " - " << fcn->getName() << " :: " << header->getName() << "\n";
      return true;
    }
  }
  return false;
}

bool SpiceProfLoad::runOnModule(Module& M)
{
  LLVM_DEBUG(errs() << "Starting SpiceProfLoad\n");


  int loopNum;

  numLoops = 0;
  inFile.open(PROF_FILE);

  if( !inFile.is_open() )
    return false;

  assert(loopNum == 0); // 0 represents the whole program, should be first

  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF)
  {
    Function &F = *IF;
    if(F.isDeclaration())
      continue;

    int curLoopNum;

    LoopInfo &li = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    list<Loop*> loops( li.begin(), li.end() );
    while( !loops.empty() )
    {
      Loop *loop = loops.front();
      if(isTargetLoop(loop)){
        runOnLoop(loop);
        ++numLoops; // calls start with integer 0
      }
      loops.pop_front();
      loops.insert( loops.end(),
          loop->getSubLoops().begin(),
          loop->getSubLoops().end());
    }
  }

  // load the input file
  __spice_read_profile();
  __spice_calculate_similarity();

  //test similarity
  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF){
    Function &F = *IF;
    if(F.isDeclaration())
      continue;
    LoopInfo &li = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    list<Loop*> loops( li.begin(), li.end() );
    while( !loops.empty() )
    {
      Loop *loop = loops.front();
      BasicBlock *header = loop->getHeader();
      for (BasicBlock::iterator ii = header->begin(), ie = header->end(); ii != ie; ++ii) {
        Instruction *inst = &*ii;
        if(!isa<PHINode>(ii))
          break;
        double pred = predictability(inst);
        errs() << "predictability for inst " << *inst << "is " << pred << "\n";
      }
      loops.pop_front();
      loops.insert( loops.end(),
          loop->getSubLoops().begin(),
          loop->getSubLoops().end());
    }
  }

  return false;
}

}
#undef DEBUG_TYPE
