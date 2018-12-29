// This file defines an abstract interface named 'PredictionSpeculation'
// This interface provides methods to query the effect of prediction
// speculation.  It does NOT tell you what to speculate.
//
// The policy of /what/ to speculate is implemented in subclasses
// of PredictionSpeculation, such as:
//  liberty::NoPredictionSpeculation and
//  liberty::SpecPriv::PredictionSpeculator
//
// Additionally, this file defines PredictionAA, which is
// an adaptor class between PredictionSpeculation and LoopAA.
#ifndef LLVM_LIBERTY_ANALYSIS_PREDICTION_SPECULATION_H
#define LLVM_LIBERTY_ANALYSIS_PREDICTION_SPECULATION_H

#include "llvm/IR/Instruction.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Analysis/LoopAA.h"

namespace liberty
{
using namespace llvm;

struct PredictionSpeculation
{
  virtual ~PredictionSpeculation() {}

  // Overload this method
  virtual bool isPredictable(const Instruction *I, const Loop *loop) = 0;

  virtual PredictionSpeculation *getPredictionSpecPtr() { return this; }
};

struct NoPredictionSpeculation : public PredictionSpeculation
{
  virtual bool isPredictable(const Instruction *I, const Loop *loop);
};

// You can use it as a LoopAA too!
struct PredictionAA : public LoopAA // Not a pass!
{
  PredictionAA(PredictionSpeculation *ps) : LoopAA(), predspec(ps) {}

  StringRef getLoopAAName() const { return "spec-priv-prediction-oracle-aa"; }

  virtual ModRefResult modref(
    const Instruction *i1,
    TemporalRelation rel,
    const Instruction *i2,
    const Loop *L);
  virtual ModRefResult modref(
    const Instruction *i1,
    TemporalRelation rel,
    const Value *P2,
    unsigned S2,
    const Loop *L);

    LoopAA::SchedulingPreference getSchedulingPreference() const
    {
      return SchedulingPreference(Low);
    }

private:
  PredictionSpeculation *predspec;
};


}
#endif

