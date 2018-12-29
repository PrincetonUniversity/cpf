#ifndef LLVM_LIBERTY_AA_VS_ORACLE
#define LLVM_LIBERTY_AA_VS_ORACLE

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/DataLayout.h"

#include <vector>

namespace liberty {
  using namespace llvm;


  #define AA_NO      0
  #define AA_MAY     1
  #define AA_MUST    2


  /// Capture, and then remove, calls to
  /// no_alias, must_alias, may_alias
  class AAvsOracle_EarlyHelper : public ModulePass {

  public:
    struct Truth
    {
      std::string     desc;
      unsigned        line;
      Value         * ptr1,
                    * ptr2;
      unsigned        s1, s2;
    };

    typedef std::vector<Truth>          Truths;
    typedef Truths::const_iterator      TI;

    static char ID;
    AAvsOracle_EarlyHelper()
      : ModulePass(ID) {}

    bool runOnModule(Module &);

    StringRef getPassName() const
    {
      return "Early helper for AA-vs-Oracle";
    }

    TI no_alias_begin() const { return no.begin(); }
    TI no_alias_end() const { return no.end(); }

    TI may_alias_begin() const { return may.begin(); }
    TI may_alias_end() const { return may.end(); }

    TI must_alias_begin() const { return must.begin(); }
    TI must_alias_end() const { return must.end(); }

  private:
    bool gather(Function *f, Truths &collection, Module &mod);

    Truths      no, may, must;
  };


  /// Compare alias analysis perforamance
  /// to a user-supplied oracle.
  class AAvsOracle : public ModulePass {

    typedef AAvsOracle_EarlyHelper::TI ATI;
    void test(unsigned truth, ATI begin, ATI end, unsigned *stats_row);

  public:

    static char ID;
    AAvsOracle()
      : ModulePass(ID) {}

    void getAnalysisUsage(AnalysisUsage &au) const
    {
      au.addRequired<AAvsOracle_EarlyHelper>();
      au.addRequired<AAResultsWrapperPass>();
      au.setPreservesAll();
    }

    bool runOnModule(Module &);

    StringRef getPassName() const
    {
      return "Compare alias analysis results to an oracle";
    }
  };
}


#endif
