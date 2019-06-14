#define DEBUG_TYPE "slamp-load"

#include "liberty/SLAMP/SLAMPLoad.h"
#include "liberty/Utilities/ModuleLoops.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/DataLayout.h"

#include <iostream>
#include <fstream>
#include <sstream>

namespace liberty
{
namespace slamp
{

using namespace std;
using namespace llvm;

char SLAMPLoadProfile::ID = 0;

static RegisterPass<SLAMPLoadProfile> RP(
  "slamp-load-profile",
   "(SLAMPLoad) Load back profile data and generate dependency information", false, false);

extern cl::opt<string> outfile; // defined in SLAMP.cpp

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
    tokens.push_back(item);
  }

  return tokens.size();
}

//moved to header file
/*
SLAMPLoadProfile::SLAMPLoadProfile() : ModulePass(ID)
{
}

SLAMPLoadProfile::~SLAMPLoadProfile()
{
}
*/

void SLAMPLoadProfile::getAnalysisUsage(AnalysisUsage& au) const
{
  au.addRequired<StaticID>();
  au.addRequired<ModuleLoops>();
  au.setPreservesAll();
}

static bool isPrimitiveSize(Type* ty, const DataLayout& td)
{
  const unsigned sz = td.getTypeStoreSize( ty );
  if (sz == 1 || sz == 2 || sz == 4 || sz == 8) return true;
  return false;
}

bool SLAMPLoadProfile::isLoopInvariantPredictionApplicable(LoadInst* li, Loop* loop)
{
  //DataLayout &td = getAnalysis< DataLayout >();
  const DataLayout &td = li->getModule()->getDataLayout();

  if (!isPrimitiveSize(li->getType(), td)) return false;

  return true;
}

bool SLAMPLoadProfile::isLinearPredictionApplicable(LoadInst* li)
{
  //DataLayout &td = getAnalysis< DataLayout >();
  const DataLayout &td = li->getModule()->getDataLayout();

  if (!li->getType()->isIntegerTy()) return false;

  if (!isPrimitiveSize(li->getType(), td)) return false;

  return true;
}

bool SLAMPLoadProfile::isLinearPredictionDoubleApplicable(LoadInst* li)
{
  //DataLayout &td = getAnalysis< DataLayout >();
  const DataLayout &td = li->getModule()->getDataLayout();

  if (!li->getType()->isDoubleTy() && !li->getType()->isFloatTy()) return false;

  if (!isPrimitiveSize(li->getType(), td)) return false;

  return true;
}

bool SLAMPLoadProfile::runOnModule(Module& m)
{
  sid = &getAnalysis< StaticID >();

  ModuleLoops& mloops = getAnalysis< ModuleLoops >();

  ifstream  ifs(outfile.c_str());

  if ( !ifs.is_open() )
  {
    errs() <<  "SLAMP output file " << outfile.c_str() << " cannot be opened\n";
    return false;
  }

  // dependencies

  string line;
  while ( getline(ifs, line) )
  {
    vector<string> tokens;
    split(line, tokens, ' ');

    assert( tokens.size() == 15 );

    uint32_t loopid = string_to<uint32_t>(tokens[0]);
    uint32_t src = string_to<uint32_t>(tokens[1]);
    uint32_t dst = string_to<uint32_t>(tokens[2]);
    uint32_t baredst = string_to<uint32_t>(tokens[3]);
    uint32_t iscross = string_to<uint32_t>(tokens[4]);
    uint64_t count = string_to<uint64_t>(tokens[5]);
    uint32_t isconstant = string_to<uint32_t>(tokens[6]);
    uint32_t size = string_to<uint32_t>(tokens[7]);
    uint64_t value = string_to<uint64_t>(tokens[8]);
    uint32_t isvalidlp = string_to<uint32_t>(tokens[9]);
    uint64_t a = string_to<uint64_t>(tokens[10]);
    uint64_t b = string_to<uint64_t>(tokens[11]);
    uint32_t isvalidlp_d = string_to<uint32_t>(tokens[12]);
    double   da = string_to<double>(tokens[13]);
    double   db = string_to<double>(tokens[14]);

    assert( src && dst );

    BasicBlock* header = sid->getBBWithID(loopid);
    assert(header);

    Function* fcn = header->getParent();
    LoopInfo& li = mloops.getAnalysis_LoopInfo(fcn);
    Loop*     loop = li.getLoopFor(header);
    if ( loop->getHeader() != header )
    {
      errs() << "Error: sid mismatch, " << loop->getHeader()->getName() << " != " << header->getName() << "\n";
      assert( false );
    }

    DepEdge edge(src, dst, iscross);

    // count

    if ( edges[loopid].count(edge) )
      edges[loopid][edge] += count;
    else
      edges[loopid][edge] = count;

    if ( baredst )
    {
      DepEdge2PredMap& edge2predmap = predictions[loopid];

      LoadInst* li = dyn_cast<LoadInst>( sid->getInstructionWithID(baredst) );
      assert(li);

      if ( !edge2predmap.count(edge) )
      {
        PredMap predmap;
        edge2predmap[edge] = predmap;
      }

      PredMap& predmap = edge2predmap[edge];
      assert( !predmap.count(li) );

      if (isconstant && isLoopInvariantPredictionApplicable(li, loop))
      {
        predmap.insert(make_pair(li, Prediction(LI_PRED)));
      }
      else if (isvalidlp && isLinearPredictionApplicable(li))
      {
        predmap.insert(make_pair(li, Prediction(LINEAR_PRED, a, b)));
      }
      else if (isvalidlp_d && isLinearPredictionDoubleApplicable(li))
      {
        I64OrDoubleValue va, vb;
        va.dval = da;
        vb.dval = db;
        predmap.insert(make_pair(li, Prediction(LINEAR_PRED_DOUBLE, va.ival, vb.ival)));
      }
      else
      {
        predmap.insert(make_pair(li, Prediction(INVALID_PRED)));
      }
    }

    // debug

    if ( DebugFlag && isCurrentDebugType(DEBUG_TYPE) )
    {
      Instruction* srcinst = sid->getInstructionWithID(src);
      Instruction* dstinst = sid->getInstructionWithID(dst);

      errs() << (iscross ? ">> Inter\n" : ">> Intra\n");
      errs() << src << " "; srcinst->dump();
      errs() << "  -->\n";
      errs() << dst << " "; dstinst->dump();
      errs() << "  : " << count << "\n\n";
    }
  }

  return true;
}

bool SLAMPLoadProfile::isTargetLoop(const Loop* loop)
{
  return ( (this->edges).count( sid->getID(loop->getHeader()) ) );
}

uint64_t SLAMPLoadProfile::numObsInterIterDep(BasicBlock* header, const Instruction* dst, const Instruction* src)
{
  uint32_t  loopid = sid->getID(header);
  uint32_t  srcid = sid->getID(src);
  uint32_t  dstid = sid->getID(dst);

  DepEdge edge(srcid, dstid, 1);
  // ZY: count == 0 stands for there is one dep
  if ((this->edges)[loopid].count(edge))
    return (this->edges)[loopid][edge] + 1;
}

uint64_t SLAMPLoadProfile::numObsIntraIterDep(BasicBlock* header, const Instruction* dst, const Instruction* src)
{
  uint32_t  loopid = sid->getID(header);
  uint32_t  srcid = sid->getID(src);
  uint32_t  dstid = sid->getID(dst);

  DepEdge edge(srcid, dstid, 0);

  // ZY: count == 1 stands for there is one dep
  if ((this->edges)[loopid].count(edge))
    return (this->edges)[loopid][edge] + 1;
  return 0;
}

bool SLAMPLoadProfile::isPredictableInterIterDep(BasicBlock* header, const Instruction* dst, const Instruction* src)
{
  uint32_t  loopid = sid->getID(header);
  uint32_t  srcid = sid->getID(src);
  uint32_t  dstid = sid->getID(dst);

  DepEdge edge(srcid, dstid, 1);

  PredMap& predmap = predictions[loopid][edge];

  if ( predmap.empty() ) return false;

  for (PredMap::iterator i=predmap.begin(), e=predmap.end() ; i!=e ; ++i)
  {
    if (i->second.type == INVALID_PRED) return false;
  }

  return true;
}

bool SLAMPLoadProfile::isPredictableIntraIterDep(BasicBlock* header, const Instruction* dst, const Instruction* src)
{
  uint32_t  loopid = sid->getID(header);
  uint32_t  srcid = sid->getID(src);
  uint32_t  dstid = sid->getID(dst);

  DepEdge edge(srcid, dstid, 0);

  PredMap& predmap = predictions[loopid][edge];

  if ( predmap.empty() ) return false;

  for (PredMap::iterator i=predmap.begin(), e=predmap.end() ; i!=e ; ++i)
  {
    if (i->second.type == INVALID_PRED) return false;
    if (i->second.type == LI_PRED) return false;
  }

  //return true;
  return false; // do not use prediction for II deps
}

PredMap SLAMPLoadProfile::getPredictions(BasicBlock* header, const Instruction* dst, const Instruction* src, bool isLC)
{
  uint32_t  loopid = sid->getID(header);
  uint32_t  srcid = sid->getID(src);
  uint32_t  dstid = sid->getID(dst);

  DepEdge edge(srcid, dstid, isLC?1:0);

  return predictions[loopid][edge];
}

}
}
