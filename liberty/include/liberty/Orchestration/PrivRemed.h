#ifndef LLVM_LIBERTY_PRIVREMED_H
#define LLVM_LIBERTY_PRIVREMED_H

#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Orchestration/Remediator.h"

namespace liberty {
using namespace llvm;

class PrivRemedy : public Remedy {
public:
  const StoreInst *storeI;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "priv-remedy"; };
};

class PrivRemediator : public Remediator {
public:
  void setPDG(PDG *loopPDG) { pdg = loopPDG; }

  StringRef getRemediatorName() const {
    return "priv-remediator";
  }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   DataDepType dataDepTy, const Loop *L);

private:
  PDG *pdg;

  bool isPrivate(const Instruction *I);
};

} // namespace liberty

#endif
