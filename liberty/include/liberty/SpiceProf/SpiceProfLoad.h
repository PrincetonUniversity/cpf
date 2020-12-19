#ifndef LLVM_LIBERTY_SPICE_PROF_LOAD_H
#define LLVM_LIBERTY_SPICE_PROF_LOAD_H

//===----------------------------------------------------------------------===//
//
// Info...
//
//===----------------------------------------------------------------------===//

#include <map>
#include <string>
#include <fstream>

#include "llvm/Pass.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Function.h"

#define SPICE_PROF_FILE "spice.prof"
#define SPICE_OUT_FILE "spice.out"

namespace llvm {
  class Loop;
}

namespace liberty {
using namespace std;
using namespace llvm;

  class SpiceProfLoad : public ModulePass {
    public:
      //profile values per invocation
      typedef vector<long> vals_t;

      //profile/count per static variable
      typedef map<int, vals_t*> InvokeVal_t;

      //map of static variable to its profile/count
      typedef map<int, InvokeVal_t*> LoopVal_t;

      //map of varId to similarity
      typedef map<int, double> similarity_t;

      static char ID;
      SpiceProfLoad() : ModulePass(ID) {};

      virtual bool runOnModule (Module &M);

      double predictability(Instruction* phi){
        if(!phiToloopNum.count(phi) || !phiTovarNum.count(phi)){
          return -1.0;
        }
        int loopId = phiToloopNum[phi];
        int varId = phiTovarNum[phi];
        similarity_t sim = *similarities[loopId];
        return sim[varId];
      }

      bool runOnLoop(Loop *Lp);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const
      {
        AU.addRequired<LoopInfoWrapperPass>();
        AU.addRequired<Targets>();
        AU.setPreservesAll();
      }
      void profile_dump(void);

    private:
      int numLoops;
      ifstream inFile;
      InvokeVal_t* curr_InvokeVal;
      map<Instruction*, int>phiToloopNum;
      map<Instruction*, int>phiTovarNum;
      map<int, LoopVal_t*> per_loop_vals;
      map <int,similarity_t*> similarities;

      double compare_loop_invokes(vals_t curr, vals_t prev);
      void __spice_calculate_similarity();
      void __spice_read_profile();
      void __spice_print_profile();
      bool isTargetLoop(Loop* l);
      void delete_loop_from_phiMap(int loopNum);

  };
}

#endif // LLVM_LIBERTY_LAMP_LOAD_PROFILE_H
