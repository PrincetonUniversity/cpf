#ifndef LLVM_LIBERTY_SPEC_PRIV_HEAP_PROFILE_LOAD_H
#define LLVM_LIBERTY_SPEC_PRIV_HEAP_PROFILE_LOAD_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/StaticID.h"

#include <map>
#include <set>

namespace liberty
{
namespace SpecPriv
{

class HeapProfileLoad : public ModulePass
{
public:
  typedef std::set<unsigned>             HeapIDs;
  typedef std::map<BasicBlock*, HeapIDs> Loop2HeapIDs;

  typedef std::map<unsigned, HeapIDs>           Stage2HeapIDs;
  typedef std::map<BasicBlock*, Stage2HeapIDs>  Loop2Stage2HeapIDs;

  typedef std::pair<Instruction*, Instruction*> Key;
  typedef std::map<Key, unsigned>::iterator     iterator;

  typedef std::set<Instruction*> InstSet;

  static char ID;
  HeapProfileLoad() : ModulePass(ID) {}
  ~HeapProfileLoad() {}

  void getAnalysisUsage(AnalysisUsage& au) const;

  bool runOnModule(Module& m);
 
  bool     isClassifiedAlloc(Instruction* instr)  { return allocs.count(instr); }
  bool     isClassifiedContext(Instruction* instr)  { return contexts.count(instr); }
  bool     allocRequiresContext(Instruction* instr) { return !alloc2contexts[instr].empty(); }
  InstSet  getAllocContextsFor(Instruction* instr) { return alloc2contexts[instr]; }
  unsigned getContextID(Instruction* ctxt) { return contexts[ctxt]; }

  HeapIDs getROHeap(Loop* loop) { return ro_heaps[loop->getHeader()]; }
  HeapIDs getNRBWHeap(Loop* loop) { return nrbw_heaps[loop->getHeader()]; }
  HeapIDs getStagePrivateHeap(Loop* loop, unsigned stage) 
  { 
    return stage_private_heaps[loop->getHeader()][stage];
  }

  unsigned getHeapID(Key key) { return key2heapid[key]; }

  iterator begin() { return key2heapid.begin(); }
  iterator end()   { return key2heapid.end(); }

private:
  InstSet                          allocs;
  std::map<Instruction*, unsigned> contexts;
  std::map<Instruction*, InstSet>  alloc2contexts;

  Loop2HeapIDs       ro_heaps;
  Loop2HeapIDs       nrbw_heaps;
  Loop2Stage2HeapIDs stage_private_heaps;

  std::map<Key, unsigned> key2heapid;
};

}
}

#endif
