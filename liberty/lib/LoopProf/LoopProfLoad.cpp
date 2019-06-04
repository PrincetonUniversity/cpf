#define DEBUG_TYPE "LoopProfLoad"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/CallSite.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"

#include "liberty/Utilities/GlobalCtors.h"

#include <iostream>
#include <fstream>
#include <set>
#include <sstream>
#include <list>

#include "liberty/LoopProf/LoopProfLoad.h"

namespace liberty
{
using namespace llvm;
using namespace std;
using namespace liberty;

STATISTIC(numNonZero, "XXX Loops with non-zero execution weight");

char LoopProfLoad::ID = 0;

const char* fname = "LoopTimes.out";

static RegisterPass<LoopProfLoad> RP10("loop-prof-load",
    "(LoopProfLoad) Load loop profiling information", false, false);

static cl::opt<bool> profDump(
    "loop-prof-dump",
    cl::init(false),
    cl::NotHidden,
    cl::desc("dump loop profiling information"));
static cl::opt<bool> assertProf(
    "loop-prof-assert",
    cl::init(false),
    cl::NotHidden,
    cl::desc("assert if loop prof yields no loops with non-zero weights"));

std::string LoopProfLoad::getLoopName(const Loop *loop) const
{
  const BasicBlock *header = loop->getHeader();
  return getLoopName(header);
}

std::string LoopProfLoad::getLoopName(const BasicBlock *header) const
{
  const Function *fcn = header->getParent();

  return (fcn->getName() + ":" + header->getName()).str();
}

void LoopProfLoad::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}



void LoopProfLoad::profile_dump(void)
{
  ofstream f;
  f.open(fname);



  errs() << "\n\nLoop Profile Info:\n";
  errs() << "0 " << totTime << " whole_program\n";

  f << "\n\nLoop Profile Info:\n";
  f << totTime << " whole_program\n";

  for( int i = 1; i <= numLoops; ++i )
  {
    string name = numToLoopName[i];

    errs() << i << " " << loopTimesMap[name] << " " << name << '\n';
    f << loopTimesMap[name] << " " << name << '\n';
  }

  f.close();
  errs() << "\n\n";
}


bool LoopProfLoad::runOnLoop(Loop *Lp)
{
  int curLoopNum;
  unsigned long exTime;

  // Use concat. of function name and loop header name to ID loop
  string name = getLoopName(Lp);

    inFile >> curLoopNum >> exTime;
  assert(curLoopNum == numLoops);

  numToLoopName[curLoopNum] = name;
  loopTimesMap[name] = exTime;

  if( exTime > 0 )
    ++numNonZero;

  return true;
}


bool LoopProfLoad::runOnModule(Module& M)
{
  DEBUG(errs() << "Starting LoopProfLoad\n");
  valid = false;
  int loopNum;

  numLoops = 0;
  inFile.open(PROF_FILE);

  if( !inFile.is_open() )
    return false;

  // Read in the first line, should be the total program time
  inFile >> loopNum >> totTime;
  assert(loopNum == 0); // 0 represents the whole program, should be first

  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF)
  {
    Function &F = *IF;
    if(F.isDeclaration())
      continue;

    // First collect all call sites in this function
    typedef std::vector<Instruction*> Calls;
    Calls calls;
    for(Function::iterator i=F.begin(), e=F.end(); i!=e; ++i)
      for(BasicBlock::iterator j=i->begin(), z=i->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;

        if( isa<CallInst>(inst) )
          calls.push_back(inst);
        else if( isa<InvokeInst>(inst) )
          calls.push_back(inst);
      }

    ++numLoops;
    int curLoopNum;
    unsigned long exTime;

    inFile >> curLoopNum >> exTime;
    assert(curLoopNum == numLoops);
    numToLoopName[curLoopNum] = F.getName();
    loopTimesMap[F.getName()] = exTime;

    LoopInfo &li = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
//    DEBUG(errs() << "Got loop info\n");

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

    // each callsite
    for(Calls::const_iterator i=calls.begin(), e=calls.end(); i!=e; ++i)
    {
      Instruction *inst = *i;

      ++numLoops;

      inFile >> curLoopNum >> exTime;
      assert(curLoopNum == numLoops);

      std::string callsitename = getCallSiteName(inst);
      numToLoopName[curLoopNum] = callsitename;
      loopTimesMap[callsitename] = exTime;
    }
  }

  DEBUG(errs() << "Finished gathering loop info\n");

  if(profDump)
    profile_dump();

  if( assertProf )
    assert( numNonZero > 0 && "Loop profile shows no loops with non-zero weight");

  valid = true;
  return false;
}

std::string LoopProfLoad::getCallSiteName(const Instruction *inst) const
{
  const BasicBlock *bb = inst->getParent();
  const Function *fcn = bb->getParent();

  const std::string name = "!callsite " + fcn->getName().str() + " " + bb->getName().str() + " ";

  if( inst->hasName() )
    return ( name + inst->getName().str() );

  else
  {
    // Offset within bb
    unsigned offset = 0;
    for(BasicBlock::const_iterator i=bb->begin(), e=bb->end(); i!=e; ++i, ++offset)
      if( inst == &*i )
        break;
    return ( name + llvm::Twine("$").str() + llvm::Twine(offset).str() );
  }
}

}
#undef DEBUG_TYPE
