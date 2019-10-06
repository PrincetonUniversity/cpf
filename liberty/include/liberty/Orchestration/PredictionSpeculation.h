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

#include <unordered_set>
#include <unordered_map>

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

  void setLoopOfInterest(Loop *L);

  virtual ModRefResult modref(const Instruction *i1, TemporalRelation rel,
                              const Instruction *i2, const Loop *L,
                              Remedies &R);
  virtual ModRefResult modref(const Instruction *i1, TemporalRelation rel,
                              const Value *P2, unsigned S2, const Loop *L,
                              Remedies &R);

  LoopAA::SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Low - 3);
    }

private:
  PredictionSpeculation *predspec;

  std::unordered_set<const Value *> predictableMemLocs;
  std::unordered_set<const Value *> nonPredictableMemLocs;
  std::unordered_map<const Value *, const Value *>
      mustAliasWithPredictableMemLocMap;

  bool mustAliasFast(const Value *ptr1, const Value *ptr2,
                     const DataLayout &DL);

  bool mustAlias(const Value *ptr1, const Value *ptr2);

  bool isPredictablePtr(const Value *ptr, const DataLayout &DL);

};


}
#endif

