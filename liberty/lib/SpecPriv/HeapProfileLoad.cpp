#include "HeapProfileLoad.h"
#include "Preprocess.h"
#include "liberty/Utilities/ModuleLoops.h"

#include <iostream>
#include "llvm/Support/CommandLine.h"

#include <iostream>
#include <fstream>
#include <sstream>

namespace liberty
{
namespace SpecPriv
{

char HeapProfileLoad::ID = 0;

static RegisterPass<HeapProfileLoad> RP(
  "hcp-load",
   "(HeapProfileLoad) Load back heap classify profile data", false, false);

// extern cl::opt<string> hcpoutfile; // defined in HeapProfile.cpp

template <class T>
static T string_to(string s)
{
  T ret;
  stringstream ss(s);
  ss >> ret;

  if (!ss)
  {
    assert(false && "Failed to convert string to given type\n");
  }

  return ret;
}

static size_t split(const string s, vector<string>& tokens, char delim)
{
  tokens.clear();

  stringstream ss(s);
  string       item;

  while ( getline(ss, item, delim) )
  {
    if ( !item.empty() ) tokens.push_back(item);
  }

  return tokens.size();
}

void HeapProfileLoad::getAnalysisUsage(AnalysisUsage& au) const
{
  //au.addRequired< DataLayout >();
  au.addRequired< StaticID >();
  au.addRequired< ModuleLoops >();
  au.addRequired< Preprocess >();
  au.setPreservesAll();
}

bool HeapProfileLoad::runOnModule(Module& m)
{
  // deprecated
  return false;

#if 0 // uncomment below once HeapProfile.cpp is fixed
  StaticID& sid = getAnalysis< StaticID >();

  ifstream  ifs(hcpoutfile.c_str());

  if ( !ifs.is_open() )
  {
    errs() <<  "Heap-classify-profile output file cannot be opened\n";
    return false;
  }

  // parse

  string line;
  while ( getline(ifs, line) )
  {
    if (line[0] == '#') continue;

    vector<string> tokens;
    split(line, tokens, ' ');

    if (tokens[0] == "total") continue;

    assert( tokens.size() == 9 );

    uint32_t loop = string_to<uint32_t>(tokens[0]);
    uint32_t context = string_to<uint32_t>(tokens[2]);
    uint32_t instr = string_to<uint32_t>(tokens[3]);
    //uint64_t size = string_to<uint32_t>(tokens[4]);
    uint32_t ro = string_to<uint32_t>(tokens[5]);
    //uint32_t sl = string_to<uint32_t>(tokens[6]);
    uint32_t nrbw = string_to<uint32_t>(tokens[7]);
    uint32_t stage = string_to<uint32_t>(tokens[8]);

    bool is_stage_private = (stage && ((stage & (stage-1)) == 0));

    // allocs

    Instruction* alloc = sid.getInstructionWithID(instr);
    assert( isa<CallInst>(alloc) || isa<InvokeInst>(alloc) );
    allocs.insert( alloc );

    // contexts

    Instruction* ctxt = NULL;
    if (context)
    {
      ctxt = sid.getInstructionWithID(context);
      assert( isa<CallInst>(ctxt) );
      unsigned ctxtid = contexts.size() + 1;
      contexts[ctxt] = ctxtid;

      alloc2contexts[alloc].insert(ctxt);
    }

    // keys

    Key key = std::make_pair(ctxt, alloc);

    if ( !key2heapid.count(key) )
    {
      unsigned sz = key2heapid.size();
      key2heapid[key] = sz+1;
    }

    // classification

    unsigned heapid = key2heapid[key];

    BasicBlock*    header = sid.getBBWithID(loop);
    HeapIDs&       ros = ro_heaps[header];
    HeapIDs&       nrbws = nrbw_heaps[header];
    Stage2HeapIDs& stage_privates = stage_private_heaps[header];

    // classification priority: ro > nrbw > stage_private

    if (ro)
    {
      ros.insert(heapid);
    }
    else if (nrbw)
    {
      nrbws.insert(heapid);
    }
    else if (is_stage_private)
    {
      int s = -1;
      while (stage)
      {
        s += 1;
        stage >>= 1;
      }
      assert(s > -1);

      stage_privates[s].insert(heapid);
    }
  }

  return false;
#endif
}

}
}
