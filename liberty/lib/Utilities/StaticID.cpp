#define DEBUG_TYPE "StaticID"

#include "llvm/Support/CommandLine.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_os_ostream.h"

#include "liberty/Utilities/StaticID.h"

#include <iostream>
#include <fstream>

namespace liberty
{

using namespace std;
using namespace llvm;

char StaticID::ID = 0;
static RegisterPass<StaticID> RP("static-id", "Assign unique static ID to each function, basic block, instruction, and global variables in the program", false, false);
static cl::opt<bool> print_load_static_id( "print-load-static-id", cl::init(false), cl::NotHidden,
    cl::desc("Print auxiliary information related to a static-id assigned load instructions") );
static cl::opt<bool> print_inst_static_id( "print-inst-static-id", cl::init(false), cl::NotHidden,
    cl::desc("Print auxiliary information related to a static-id assigned instructions") );

void StaticID::getAnalysisUsage(AnalysisUsage& au) const
{
  au.setPreservesAll();
}

bool StaticID::runOnModule(Module& m)
{
  uint32_t func_id = 1;
  uint32_t bb_id = 1;
  uint32_t inst_id = 1;
  uint32_t load_id = 1;

  ofstream load_static_id;
  ofstream inst_static_id;

  std::string crit_edge_str = "crit_edge";

  if (print_load_static_id)
  {
    load_static_id.open("load-static-id.txt");
  }

  if (print_inst_static_id)
  {
    inst_static_id.open("inst-static-id.txt");
  }

  for (Module::iterator fi = m.begin(), fe = m.end() ; fi != fe ; fi++)
  {
    Function* func = &*fi;

    if (func->isDeclaration())
    {
      continue;
    }

    id_func_map[func_id] = func;
    func_id_map[func] = func_id++;

    uint32_t func_bb_id = 1;

    for (Function::iterator bi = func->begin() ; bi != func->end() ; bi++)
    {
      // sot: ignore dummy basic blocks created to break crit_edges
      std::string bbName = (&*bi)->getName().str();
      if (bbName.length() >= crit_edge_str.length())
        if (bbName.compare (bbName.length() - crit_edge_str.length(), crit_edge_str.length(), crit_edge_str) == 0)
          continue;

      id_bb_map[bb_id] = &*bi;
      bb_id_map[&*bi] = bb_id++;

      func_local_id_bb_map[func][func_bb_id] = &*bi;
      func_bb_id_map[func][&*bi] = func_bb_id++;
    }

    uint32_t func_inst_id = 1;

    for (inst_iterator ii = inst_begin(func) ; ii != inst_end(func) ; ii++)
    {
      // sot: ignore dummy basic blocks created to break crit_edges
      std::string bbName = (&*ii)->getParent()->getName().str();
      if (bbName.length() >= crit_edge_str.length())
        if (bbName.compare (bbName.length() - crit_edge_str.length(), crit_edge_str.length(), crit_edge_str) == 0)
          continue;

      func_local_id_inst_map[func][func_inst_id] = &*ii;
      func_local_inst_id_map[func][&*ii] = func_inst_id++;

      if (inst_id == 0)
      {
        assert(false && "inst_id overflowed\n");
      }

      if (print_inst_static_id)
      {
        inst_static_id << "static inst id: " << inst_id;
        inst_static_id << " function: " << func->getName().str();
        inst_static_id << " basic block: " << (*ii).getParent()->getName().str();
        inst_static_id << " instruction: ";

        //#if 0
        //{
          string inst_string;
          raw_string_ostream os(inst_string);
          (*ii).print(os);
          inst_static_id << inst_string;
        //}
        //#endif

        inst_static_id << "\n";
      }

      id_inst_map[inst_id] = &*ii;
      inst_id_map[&*ii] = inst_id++;

      if ( isa<LoadInst>(&*ii) )
      {
        if (print_load_static_id)
        {
          load_static_id << "static load id: " << load_id;
          load_static_id << " function: " << func->getName().str();
          load_static_id << " basic block: " << (*ii).getParent()->getName().str();
          load_static_id << " instruction: ";

          string inst_string;
          raw_string_ostream os(inst_string);
          (*ii).print(os);

          load_static_id << inst_string << "\n";
        }

        id_load_map[load_id] = cast<LoadInst>(&*ii);
        load_id_map[cast<LoadInst>(&*ii)] = load_id++;
      }
    }
  }

  uint32_t gv_id = 1;

  for (Module::global_iterator gi = m.global_begin() ; gi != m.global_end() ; gi++)
  {
    id_gv_map[gv_id] = &*gi;
    gv_id_map[&*gi] = gv_id++;
  }

  if (print_load_static_id)
  {
    load_static_id.close();
  }
  if (print_inst_static_id)
  {
    inst_static_id.close();
  }

  return true;
}

}
