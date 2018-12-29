#ifndef LLVM_LIBERTY_SLAMP_SLAMPLOAD_H
#define LLVM_LIBERTY_SLAMP_SLAMPLOAD_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/StaticID.h"

#include <map>
#include <set>

namespace liberty
{
namespace slamp
{

using namespace std;
using namespace llvm;

enum PredType {
  INVALID_PRED,
  LI_PRED,
  LINEAR_PRED,
  LINEAR_PRED_DOUBLE
};

struct Prediction
{
  PredType type;

  // for linear prediction

  int64_t a;
  int64_t b;

  Prediction(PredType t) : type(t) {}
  Prediction(PredType t, int64_t a, int64_t b) : type(t), a(a), b(b) {}
};

typedef std::map<LoadInst*, Prediction> PredMap;

//sot
//class llvm::ModulePass;
ModulePass *createSLAMPLoadProfilePass();

class SLAMPLoadProfile : public ModulePass
{
public:
  static char ID;
  //sot
  SLAMPLoadProfile() : ModulePass(ID) {};
  ~SLAMPLoadProfile() {};

  void getAnalysisUsage(AnalysisUsage& au) const;

  bool runOnModule(Module& m);

  bool isTargetLoop(const Loop* loop);

  uint64_t numObsInterIterDep(BasicBlock* header, const Instruction* dst, const Instruction* src);
  uint64_t numObsIntraIterDep(BasicBlock* header, const Instruction* dst, const Instruction* src);
  bool     isPredictableInterIterDep(BasicBlock* header, const Instruction* dst, const Instruction* src);
  bool     isPredictableIntraIterDep(BasicBlock* header, const Instruction* dst, const Instruction* src);
  PredMap  getPredictions(BasicBlock* header, const Instruction* dst, const Instruction* src, bool isLC);

private:
  class DepEdge
  {
  public:
    DepEdge(uint32_t s, uint32_t d, uint32_t c) : src(s), dst(d), cross(c) {}

    uint32_t src;
    uint32_t dst;
    uint32_t cross;
  };

  struct DepEdgeComp
  {
    bool operator()(const DepEdge& e1, const DepEdge& e2) const
    {
      if (e1.src < e2.src)
        return true;
      else if (e1.src > e2.src)
        return false;

      if (e1.dst < e2.dst)
        return true;
      else if (e1.dst > e2.dst)
        return false;

      return e1.cross < e2.cross;
    }
  };

  typedef union
  {
    int64_t ival;
    double  dval;
  } I64OrDoubleValue;

  StaticID* sid;

  typedef map<DepEdge, uint64_t, DepEdgeComp> DepEdgeMap;
  map<uint32_t, DepEdgeMap> edges;

  typedef map<DepEdge, PredMap, DepEdgeComp> DepEdge2PredMap;
  map<uint32_t, DepEdge2PredMap> predictions;

  bool isLoopInvariantPredictionApplicable(LoadInst* li, Loop* loop);
  bool isLinearPredictionApplicable(LoadInst* li);
  bool isLinearPredictionDoubleApplicable(LoadInst* li);
};

}
}

#endif
