// This is an adaptor class from a heap assignment
// to the AA stack.  It applies disjoint heap reasoning
// as a separation AA.  When applied to a PDG, it
// removes edges which will be speculated and validated
// by code that uses that heap assignment.
#ifndef LIBERTY_SPEC_PRIV_LOCALITY_ORACLE_AA_H
#define LIBERTY_SPEC_PRIV_LOCALITY_ORACLE_AA_H

#include "scaf/MemoryAnalysisModules/ClassicLoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Read.h"
#include "scaf/Utilities/GetMemOper.h"

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

class LocalityRemedy : public Remedy {
public:
  // application of separation logic requires 3 checks for misspec:
  // 1) ensure separation to the 5 families
  // 2) guard all accesses to private objects with private_read and
  // private_store function calls
  // 3) ensure that short-lived objects are not live at the end of iteration

  // eventually might need to keep the private and local AUs (of type Ptrs
  // defined in Pieces.h). Also need to keep all the insts involved in private
  // accesses. These insts need to be guarded
  //
  // maybe need split to multiple separated remedies. For example,
  // separation cost maybe should be separated from cost for private objects.
  // remedies maybe need to also record all the AUs assumed to be part of a
  // family. Heap assignment might be adjusted based on the selected remedies.
  Instruction *privateI;
  const LoadInst *privateLoad;
  StoreInst *reduxS;
  // const Instruction *localI;

  // if a pointer was identified as private, redux or local for removal of
  // loop-carried dep, record in 'ptr'
  Value *ptr;

  // record both pointers involved in a dependence when separation logic is used
  Value *ptr1;
  Value *ptr2;

  enum LocalityRemedType {
    ReadOnly = 0,
    Redux,
    Local,
    Private,
    KillPriv,
    SharePriv,
    Separated,
    Subheaps,
    UOCheck,
    LocalityAA
  };

  LocalityRemedType type;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  void setCost(PerformanceEstimator *perf);
  StringRef getRemedyName() const { return "locality-remedy"; };

  StringRef getLocalityRemedyName() const {
    switch (type) {
    case ReadOnly:
      return "locality-readonly-remedy";
      break;
    case Redux:
      return "locality-redux-remedy";
      break;
    case Local:
      return "locality-local-remedy";
      break;
    case Private:
      return "locality-private-remedy";
      break;
    case KillPriv:
      return "locality-kill-priv-remedy";
      break;
    case SharePriv:
      return "locality-share-priv-remedy";
      break;
    case Separated:
      return "locality-separated-remedy";
      break;
    case Subheaps:
      return "locality-subheaps-remedy";
      break;
    case UOCheck:
      return "locality-uocheck-remedy";
      break;
    case LocalityAA:
      return "locality-aa-remedy";
      break;
    default:
      assert(false && "No locality-remedy type?");
      return "NO REMEDY TYPE?";
    }
  };

  bool isExpensive() {
    if (type == Private)
      return true;
    else
      return false;
  }
};

/// Adapts separation speculation to LoopAA.
struct LocalityAA : public LoopAA,
                    Remediator // Not a pass!
{
  LocalityAA(const Read &rd, const HeapAssignment &ha, const Ctx *cx,
             PerformanceEstimator *pf)
      : LoopAA(), read(rd), asgn(ha), ctx(cx), perf(pf) {}

  StringRef getLoopAAName() const { return "spec-priv-locality-oracle-aa"; }

  StringRef getRemediatorName() const {
    return "spec-priv-locality-oracle-remed";
  }

  LoopAA::AliasResult alias(const Value *P1, unsigned S1, TemporalRelation rel,
                            const Value *P2, unsigned S2, const Loop *L,
                            Remedies &R,
                            DesiredAliasResult dAliasRes = DNoOrMustAlias);

  LoopAA::ModRefResult modref(const Instruction *A, TemporalRelation rel,
                              const Value *ptrB, unsigned sizeB, const Loop *L,
                              Remedies &R);

  LoopAA::ModRefResult modref(const Instruction *I1, TemporalRelation Rel,
                              const Instruction *I2, const Loop *L,
                              Remedies &remeds);

  LoopAA::ModRefResult modref_with_ptrs(const Instruction *A, const Value *ptrA,
                                        TemporalRelation rel,
                                        const Instruction *B, const Value *ptrB,
                                        const Loop *L, Remedies &R);

  LoopAA::SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Bottom + 4);
  }

private:
  const Read &read;
  const HeapAssignment &asgn;
  const Ctx *ctx;
  PerformanceEstimator *perf;

  unordered_set<const Value *> privateInsts;

  void populateCheapPrivRemedies(Ptrs aus, Remedies &R);
  void populateNoWAWRemedies(Ptrs aus, Remedies &R);
};

} // namespace liberty

#endif

