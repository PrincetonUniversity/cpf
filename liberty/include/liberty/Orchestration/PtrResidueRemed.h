#ifndef LLVM_LIBERTY_PTR_RESIDUE_REMED_H
#define LLVM_LIBERTY_PTR_RESIDUE_REMED_H

#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Orchestration/Remediator.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/PtrResidueManager.h"

namespace liberty {
using namespace llvm;

class PtrResidueRemedy : public Remedy {
public:
  const Value *ptr1;
  const Ctx *ctx1;
  const Value *ptr2;
  const Ctx *ctx2;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "ptr-residue-remedy"; };
};

class PtrResidueRemediator : public Remediator {
public:
  PtrResidueRemediator(SpecPriv::PtrResidueSpeculationManager *man)
      : Remediator(), manager(man) {
    td = nullptr;
  }

  StringRef getRemediatorName() const {
    return "ptr-residue-remediator";
  }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   DataDepType dataDepTy, const Loop *L);

private:
  const DataLayout *td;
  SpecPriv::PtrResidueSpeculationManager *manager;

  /// Can there be an alias?  If so, report necessary assumptions
  bool may_alias(const Value *P1, unsigned S1, const Value *P2, unsigned S2,
                 const Loop *L,
                 PtrResidueSpeculationManager::Assumption &a1_out,
                 PtrResidueSpeculationManager::Assumption &a2_out) const;

  /// Can there be a mod-ref?  If so, report necessary assumptions
  bool may_modref(const Instruction *A, const Value *ptrB, unsigned sizeB,
                  const Loop *L,
                  PtrResidueSpeculationManager::Assumption &a1_out,
                  PtrResidueSpeculationManager::Assumption &a2_out) const;
};

} // namespace liberty

#endif
