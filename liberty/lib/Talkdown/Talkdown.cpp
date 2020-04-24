#define DEBUG_TYPE "talkdown"

#include "SystemHeaders.hpp"

#include "llvm/Analysis/LoopInfo.h"

#include "liberty/LoopProf/Targets.h"

#include "liberty/Talkdown/Talkdown.h"

#include <iostream>
#include <map>
#include <string>

using namespace llvm;
using namespace AutoMP;

namespace llvm
{
  /*
   * Options for talkdown
   */
  static cl::opt<bool> TalkdownDisable("noelle-talkdown-disable", cl::ZeroOrMore, cl::Hidden, cl::desc("Disable Talkdown"));

  bool Talkdown::runOnModule(Module &M)
  {
    bool modified = false;

    if ( !this->enabled )
      return false;

    liberty::ModuleLoops &mloops = getAnalysis<liberty::ModuleLoops>();

    // TESTING
#if 0
    std::cerr << "Functions in module:\n";
    for ( auto &f : M )
    {
      if ( f.isDeclaration() )
        continue;
      LLVM_DEBUG(std::cerr << "\t" << f.getName().str() << "\n";);
    }

    // TESTING
    for ( auto &f : M )
    {
      // skip going through function declarations
      if ( f.isDeclaration() )
        continue;

      LoopInfo &loop_info = getAnalysis<LoopInfoWrapperPass>(f).getLoopInfo();

      // print each loop in function
      errs() << "Loops in function " << f.getName().str() << ":\n";
      for ( auto &l : loop_info )
        errs() << *l << "\n";
      errs() << "\n";

      // go thru each bb and print what loop it is in
      for ( auto &bb : f )
      {
        errs() << "Loop info for basic block " << *&bb << ":\n";
        Loop *l = loop_info.getLoopFor( &bb );
        if ( l )
        {
          int depth = loop_info.getLoopDepth( &bb );
          errs() << "At loop depth " << depth << "\n\n";
        }
        else
          errs() << "\tDoes not belong to any loop\n\n";
      }

    }
#endif

    // construct tree for each function
    for ( auto &f : M )
    {
      if ( f.isDeclaration() )
        continue;

      LoopInfo &loop_info = getAnalysis<LoopInfoWrapperPass>(f).getLoopInfo();
      FunctionTree tree = FunctionTree( &f );
      modified |= tree.constructTree( &f, loop_info );
      function_trees.push_back( tree );
    }

    /* std::cerr << "Should be initialized\n"; */
    /* std::cerr << "There are " << function_trees.size() << " function trees\n"; */

    LLVM_DEBUG(
    errs() << "\n-------- Begin printing of function trees --------\n";
    for ( auto &tree : function_trees )
    {
      std::cerr << tree;
    }
    errs() << "\n-------- Done printing function trees --------\n";
    );

    return false;
  }

  void Talkdown::getAnalysisUsage(AnalysisUsage &AU) const
  {
    // XXX: Add this back in once we have the actual loops to target
    /* AU.addRequired<liberty::Targets>(); */
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<liberty::ModuleLoops>();
    AU.setPreservesAll();
  }

  bool Talkdown::doInitialization(Module &M)
  {
		this->enabled = (TalkdownDisable.getNumOccurrences() == 0);
    return false;
  }

} // namespace llvm

char llvm::Talkdown::ID = 0;
static RegisterPass<Talkdown> X("Talkdown", "The Talkdown pass");

// Register pass with Clang
static Talkdown * _PassMaker = NULL;
static RegisterStandardPasses _RegPass1(PassManagerBuilder::EP_OptimizerLast,
    [](const PassManagerBuilder&, legacy::PassManagerBase& PM) {
        if(!_PassMaker){ PM.add(_PassMaker = new Talkdown());}}); // ** for -Ox
static RegisterStandardPasses _RegPass2(PassManagerBuilder::EP_EnabledOnOptLevel0,
    [](const PassManagerBuilder&, legacy::PassManagerBase& PM) {
        if(!_PassMaker){ PM.add(_PassMaker = new Talkdown());}});// ** for -O0
