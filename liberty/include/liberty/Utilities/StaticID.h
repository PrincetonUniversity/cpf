#ifndef LLVM_LIBERTY_STATIC_ID_H
#define LLVM_LIBERTY_STATIC_ID_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include <stdint.h>

#include <map>

namespace liberty
{

using namespace std;
using namespace llvm;

class StaticID: public ModulePass
{
public:
  static char ID;
  StaticID() : ModulePass(ID) {}
  ~StaticID() {}

  void getAnalysisUsage(AnalysisUsage& au) const;

  bool runOnModule(Module& m);

  uint32_t getID(const Function* func) 
  { 
    if ( hasID(func) )
      return func_id_map[func];
    else
      return 0;
  }
  uint32_t getID(const BasicBlock* bb)
  {
    if ( hasID(bb) )
      return bb_id_map[bb];
    else
      return 0;
  }
  uint32_t getID(const Function* func, const BasicBlock* bb)
  {
    if ( hasID(func, bb) )
      return func_bb_id_map[func][bb];
    else
      return 0;
  }
  uint32_t getID(const Instruction* inst)
  {
    if ( hasID(inst) )
      return inst_id_map[inst];
    else
      return 0;
  }
  uint32_t getID(const GlobalVariable* gv)
  {
    if ( hasID(gv) )
      return gv_id_map[gv];
    else
      return 0;
  }
  uint32_t getLoadID(const LoadInst* li)
  {
    if ( hasLoadID(li) )
      return load_id_map[li];
    else
      return 0;
  }

  Function* getFuncWithID(uint32_t id)
  {
    if (id_func_map.find(id) == id_func_map.end())
      return NULL;
    else
      return id_func_map[id];
  }

  BasicBlock* getBBWithID(uint32_t id)
  {
    if (id_bb_map.find(id) == id_bb_map.end())
      return NULL;
    else
      return id_bb_map[id];
  }

  BasicBlock* getBBWithFuncLocalID(const Function* f, uint32_t id)
  {
    if ( func_local_id_bb_map.find(f) == func_local_id_bb_map.end() )
      return NULL;

    map<uint32_t, BasicBlock*>& m = func_local_id_bb_map[f];
    if (m.find(id) == m.end())
      return NULL;
    else
      return m[id];
  }

  Instruction* getInstructionWithID(uint32_t id)
  {
    if (id_inst_map.find(id) == id_inst_map.end())
      return NULL;
    else
      return id_inst_map[id];
  }

  Instruction* getInstWithFuncLocalID(const Function* f, uint32_t id)
  {
    if ( func_local_id_inst_map.find(f) == func_local_id_inst_map.end() )
      return NULL;

    map<uint32_t, Instruction*>& m = func_local_id_inst_map[f];
    if (m.find(id) == m.end())
      return NULL;
    else
      return m[id];
  }

  uint32_t getFuncLocalIDWithInst(const Function* f, const Instruction* inst)
  {
    if ( func_local_inst_id_map.find(f) == func_local_inst_id_map.end() )
      return 0;

    map<const Instruction*, uint32_t>& m = func_local_inst_id_map[f];
    if (m.find(inst) == m.end())
      return 0;
    else
      return m[inst];
  }

  LoadInst* getLoadInstWithID(uint32_t id)
  {
    if (id_load_map.find(id) == id_load_map.end())
      return NULL;
    else
      return id_load_map[id];
  }

  GlobalVariable* getGVtWithID(uint32_t id)
  {
    if (id_gv_map.find(id) == id_gv_map.end())
      return NULL;
    else
      return id_gv_map[id];
  }

  bool hasID(const Function* func) { return func_id_map.find(func) != func_id_map.end(); }
  bool hasID(const BasicBlock* bb) { return bb_id_map.find(bb) != bb_id_map.end(); }
  bool hasID(const Function* func, const BasicBlock* bb) 
  {
    if ( func_bb_id_map.find(func) == func_bb_id_map.end() )
      return false;

    return func_bb_id_map[func].find(bb) != func_bb_id_map[func].end();
  }
  bool hasID(const Instruction* inst) { return inst_id_map.find(inst) != inst_id_map.end(); }
  bool hasID(const GlobalVariable* gv) { return gv_id_map.find(gv) != gv_id_map.end(); }
  bool hasLoadID(const LoadInst* li) { return load_id_map.find(li) != load_id_map.end(); }

  uint64_t getMaxFuncID() { return func_id_map.size(); }
  uint64_t getMaxBBID() { return bb_id_map.size(); }
  uint64_t getMaxBBID(Function* func)
  {
    if ( func_bb_id_map.find(func) == func_bb_id_map.end() )
      return 0;

    return func_bb_id_map[func].size();
  }
  uint64_t getMaxInstID() { return inst_id_map.size(); }
  uint64_t getMaxGVID() { return gv_id_map.size(); }
  uint64_t getMaxLoadID() { return load_id_map.size(); }

private:
  map<const Function*, uint32_t> func_id_map;
  map<const BasicBlock*, uint32_t> bb_id_map;
  map<const Function*, map<const BasicBlock*, uint32_t> > func_bb_id_map;
  map<const Instruction*, uint32_t> inst_id_map;
  map<const GlobalVariable*, uint32_t> gv_id_map;
  map<const LoadInst*, uint32_t> load_id_map;

  map<uint32_t, Function*> id_func_map;
  map<uint32_t, BasicBlock*> id_bb_map;
  map<uint32_t, Instruction*> id_inst_map;
  map<uint32_t, GlobalVariable*> id_gv_map;
  map<uint32_t, LoadInst*> id_load_map;

  map<const Function*, map<uint32_t, BasicBlock*> > func_local_id_bb_map;
  map<const Function*, map<uint32_t, Instruction*> > func_local_id_inst_map;
  map<const Function*, map<const Instruction*, uint32_t> > func_local_inst_id_map;

};

}

#endif
