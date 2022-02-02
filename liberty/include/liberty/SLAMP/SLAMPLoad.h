#ifndef LLVM_LIBERTY_SLAMP_SLAMPLOAD_H
#define LLVM_LIBERTY_SLAMP_SLAMPLOAD_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Pass.h"

#include "liberty/Utilities/StaticID.h"

#include <map>
#include <set>

namespace liberty::slamp {

using namespace std;
using namespace llvm;

enum PredType { INVALID_PRED, LI_PRED, LINEAR_PRED, LINEAR_PRED_DOUBLE };

struct Prediction {
  PredType type;

  // for linear prediction

  int64_t a;
  int64_t b;

  Prediction(PredType t) : type(t) {}
  Prediction(PredType t, int64_t a, int64_t b) : type(t), a(a), b(b) {}
};

using PredMap = std::map<LoadInst *, Prediction>;

// sot
// class llvm::ModulePass;
ModulePass *createSLAMPLoadProfilePass();

class SLAMPLoadProfile : public ModulePass {
public:
  static char ID;
  // sot
  SLAMPLoadProfile() : ModulePass(ID){};
  ~SLAMPLoadProfile(){};

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &m);

  bool isTargetLoop(const Loop *loop);

  uint64_t numObsInterIterDep(BasicBlock *header, const Instruction *dst,
                              const Instruction *src);
  uint64_t numObsIntraIterDep(BasicBlock *header, const Instruction *dst,
                              const Instruction *src);
  bool isPredictableInterIterDep(BasicBlock *header, const Instruction *dst,
                                 const Instruction *src);
  bool isPredictableIntraIterDep(BasicBlock *header, const Instruction *dst,
                                 const Instruction *src);
  PredMap getPredictions(BasicBlock *header, const Instruction *dst,
                         const Instruction *src, bool isLC);

private:
  class DepEdge {
  public:
    DepEdge(uint32_t s, uint32_t d, uint32_t c) : src(s), dst(d), cross(c) {}

    uint32_t src;
    uint32_t dst;
    uint32_t cross;
  };

  struct DepEdgeComp {
    bool operator()(const DepEdge &e1, const DepEdge &e2) const {
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

  using I64OrDoubleValue = union {
    int64_t ival;
    double dval;
  };

  StaticID *sid;

  using DepEdgeMap = map<DepEdge, uint64_t, DepEdgeComp>;
  map<uint32_t, DepEdgeMap> edges;

  using DepEdge2PredMap = map<DepEdge, PredMap, DepEdgeComp>;
  map<uint32_t, DepEdge2PredMap> predictions;

  bool isLoopInvariantPredictionApplicable(LoadInst *li);
  bool isLinearPredictionApplicable(LoadInst *li);
  bool isLinearPredictionDoubleApplicable(LoadInst *li);
  uint64_t numObsDep(BasicBlock *header, const Instruction *dst,
                     const Instruction *src, bool crossIter);
};

} // namespace liberty::slamp

#endif
