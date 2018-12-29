#ifndef LLVM_LIBERTY_ANALYSIS_REDUCTION_DETECTION_H
#define LLVM_LIBERTY_ANALYSIS_REDUCTION_DETECTION_H

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"

#include "typedefs.h"


namespace liberty
{
using namespace llvm;

struct ReductionDetection {

  bool isSumReduction(const Loop *loop, const Instruction *src,
                      const Instruction *dst, const bool loopCarried);
  bool isMinMaxReduction(const Loop *loop, const Instruction *src,
                         const Instruction *dst, const bool loopCarried);
};
}

#endif
