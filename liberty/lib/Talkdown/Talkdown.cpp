#define DEBUG_TYPE "talkdown"

#include "SystemHeaders.hpp"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/CommandLine.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/Utilities/ReportDump.h"

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
  // This was useful in noelle. But in cpf we have the `aa` script that we can add or remove LoopAA passes to/from
  static cl::opt<bool> TalkdownDisable("talkdown-disable", cl::ZeroOrMore, cl::Hidden, cl::desc("Disable Talkdown"));
  static cl::opt<bool> PrintFunctionTrees("print-function-trees", cl::ZeroOrMore, cl::Hidden, cl::desc("Print out function trees"));

  bool Talkdown::runOnModule(Module &M)
  {
    bool modified = false;

    if ( !this->enabled )
      return false;

    liberty::ModuleLoops &mloops = getAnalysis<liberty::ModuleLoops>();

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

    // maybe use REPORT_DUMP for this?
    if ( PrintFunctionTrees.getNumOccurrences() != 0 )
    {
      errs() << "\n-------- Begin printing of function trees --------\n";
      for ( auto &tree : function_trees )
      {
        llvm::errs() << tree;
      }
      errs() << "\n-------- Done printing function trees --------\n";
    }

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

  const AnnotationSet &Talkdown::getAnnotationsForInst(const Instruction *i) const
  {
    assert(0);
  }

  const AnnotationSet &Talkdown::getAnnotationsForInst(const Instruction *i, const Loop *l) const
  {
    Function *f = l->getHeader()->getParent();
    const FunctionTree &tree = findTreeForFunction( f );
    return tree.getAnnotationsForInst(i, l);
  }

  const FunctionTree &Talkdown::findTreeForFunction(Function *f) const
  {
    for ( const auto &ft : function_trees )
    {
      auto *af = ft.getFunction();
      assert( af && "Could not find function tree in Talkdown" );
      if ( f == af )
        return ft;
    }
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
