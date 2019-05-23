#ifndef LLVM_LIBERTY_ANALYSIS_REDUCTION_DETECTION_H
#define LLVM_LIBERTY_ANALYSIS_REDUCTION_DETECTION_H

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"

#include "typedefs.h"

#include "PDG.hpp"

#include<unordered_set>

namespace liberty
{
using namespace llvm;

struct MinMaxReductionInfo {
  const CmpInst *cmpInst;
  const Value *minMaxValue;

  // Is the running min/max the first or second operand of the compare?
  bool isFirstOperand;

  // Does the compare return true or false when finding a new min/max?
  bool cmpTrueOnMinMax;

  // Other values that are live out with the min/max
  ValueList reductionLiveOuts;

  /// List of edges affected by this reduction.
  typedef std::vector<DGEdge<Value *> *> Edges;
  Edges edges;
};

struct ReductionDetection {

  bool isSumReduction(const Loop *loop, const Instruction *src,
                      const Instruction *dst, const bool loopCarried);
  bool isMinMaxReduction(const Loop *loop, const Instruction *src,
                         const Instruction *dst, const bool loopCarried);

  void findMinMaxRegReductions(Loop *loop, PDG *pdg);

private:
  std::unordered_set<const Instruction *> minMaxReductions;
};
}

#endif
