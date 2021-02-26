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

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instruction.h"

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Strategy/PerformanceEstimator.h"

#include <unordered_map>
#include <unordered_set>

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;
using namespace SpecPriv;

struct PredictionSpeculation {
  virtual ~PredictionSpeculation() {}

  // Overload this method
  virtual bool isPredictable(const Instruction *I, const Loop *loop) = 0;

  virtual PredictionSpeculation *getPredictionSpecPtr() { return this; }
};

struct NoPredictionSpeculation : public PredictionSpeculation {
  virtual bool isPredictable(const Instruction *I, const Loop *loop);
};

class LoadedValuePredRemedy : public Remedy {
public:
  const Value *ptr; // pointer of loop-invariant load instruction
  bool write;
  const Instruction *loadI;

  // void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  void setCost(PerformanceEstimator *perf, const Loop *loop);
  StringRef getRemedyName() const { return "invariant-value-pred-remedy"; };
};

// You can use it as a LoopAA too!
struct PredictionAA : public LoopAA // Not a pass!
{
  PredictionAA(PredictionSpeculation *ps, PerformanceEstimator *pf)
      : LoopAA(), predspec(ps), perf(pf) {}

  StringRef getLoopAAName() const { return "spec-priv-prediction-oracle-aa"; }

  void setLoopOfInterest(Loop *L);

  virtual LoopAA::AliasResult
  alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
        const Value *ptrB, unsigned sizeB, const Loop *L, Remedies &R,
        DesiredAliasResult dAliasRes = DNoOrMustAlias);

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
  const DataLayout *DL;
  const Loop *L;
  PerformanceEstimator *perf;

  std::unordered_set<const Value *> predictableMemLocs;
  std::unordered_set<const Value *> nonPredictableMemLocs;
  std::unordered_map<const Value *, const Value *>
      mustAliasWithPredictableMemLocMap;
  std::unordered_map<const Value *, const Instruction *> mapPtrsToLoad;

  bool mustAliasFast(const Value *ptr1, const Value *ptr2);

  bool mustAlias(const Value *ptr1, const Value *ptr2);

  bool isPredictablePtr(const Value *ptr);
};

} // namespace liberty
#endif

