#ifndef LLVM_LIBERTY_UTILITIES_FUNCTION_LEVEL_DOMINATOR_H
#define LLVM_LIBERTY_UTILITIES_FUNCTION_LEVEL_DOMINATOR_H

#include <stdint.h>

#include <map>
#include <vector>

#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"

namespace liberty
{

using namespace std;
using namespace llvm;

class FunctionLevelDominator
{
  public: 
    FunctionLevelDominator(Function* func, bool post = false);
    ~FunctionLevelDominator() 
    { 
      for (unsigned i = 0 ; i < bbnum ; i++)
      {
        if (post)
        {
          free(pdommap[i]);
          free(pdommap_ignore_unreachable[i]);
        }
        else
        {
          free(dommap[i]);
        }
      }
    }
    
    bool dominates(Instruction* i1, Instruction* i2);
    bool dominates(BasicBlock* b1, BasicBlock* b2);

    bool strictlyDominates(Instruction* i1, Instruction* i2);
    bool strictlyDominates(BasicBlock* b1, BasicBlock* b2);

    bool postdominates(Instruction* i1, Instruction* i2, bool ignore_unreachable = false);
    bool postdominates(BasicBlock* b1, BasicBlock* b2, bool ignore_unreachable = false);

    bool strictlyPostdominates(Instruction* i1, Instruction* i2, bool ignore_unreachable = false);
    bool strictlyPostdominates(BasicBlock* b1, BasicBlock* b2, bool ignore_unreachable = false);

    bool isInDominanceFrontier(BasicBlock* b1, BasicBlock* b2);

    bool isInPostdominanceFrontier(BasicBlock* b1, BasicBlock* b2);

    void computeImmediateDominator();
    void computeImmediatePostdominator();

    BasicBlock* getImmediateDominator(BasicBlock* bb);
    BasicBlock* getImmediatePostdominator(BasicBlock* bb);

    void computeDominanceFrontier();
    void computePostdominanceFrontier();

  private:
    Function* func;
    bool      post;

    unsigned bbnum;
    unsigned bitvec_size;

    bool     idommap_valid;
    bool     ipdommap_valid;

    map<BasicBlock*, unsigned> idmap;
    map<unsigned, BasicBlock*> revidmap;

    vector<uint64_t*> dommap;
    vector<uint64_t>  idommap;
    vector<uint64_t*> dfmap;

    vector<uint64_t*> pdommap;
    vector<uint64_t*> pdommap_ignore_unreachable;
    vector<uint64_t>  ipdommap;
    vector<uint64_t*> pdfmap;

    bool dominates(unsigned b1id, unsigned b2id);
    bool postdominates(unsigned b1id, unsigned b2id, bool ignore_unreachable = false);

    bool isInDominanceFrontier(unsigned b1id, unsigned b2id);
    bool isInPostdominanceFrontier(unsigned b1id, unsigned b2id);

    void computeDominator(Function* func);
    void computePostdominator(Function* func, bool ignore_unreachable, vector<uint64_t*>& m);

    void setdom(unsigned bb, unsigned dom, vector<uint64_t*>& map);
    void setdomall(unsigned bb, vector<uint64_t*>& map);
    void intersect(uint64_t* dst, uint64_t* src0, uint64_t* src1);
    void dump();
};

}

#endif
