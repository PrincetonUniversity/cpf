// This file defines the edge count oracle, which is
// an adaptor between ControlSpeculation and the LoopAA
// stack.  Please note that this class is NOT an llvm::Pass.
// You must manually instantiate it with a concrete
// ControlSpeculation, and manually insert it into the
// LoopAA stack.
//
// See also: include/liberty/Analysis/ControlSpeculation.h
#ifndef LLVM_LIBERTY_SPEC_PRIV_EDGE_COUNT_ORACLE_AA_H
#define LLVM_LIBERTY_SPEC_PRIV_EDGE_COUNT_ORACLE_AA_H

#include "llvm/IR/Instructions.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ControlSpeculation.h"

#include <set>

namespace liberty
{
using namespace llvm;


// Serves as an adaptor between LoopAA and ControlSpeculator
struct EdgeCountOracle : public LoopAA // Not a pass!
{
  EdgeCountOracle(ControlSpeculation *prof) : LoopAA(), speculator(prof) {}

  StringRef getLoopAAName() const { return "edge-count-oracle-aa"; }

  ModRefResult modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L);

  ModRefResult modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L);

  LoopAA::SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Low);
  }

private:
  ControlSpeculation *speculator;
};


}

#endif

