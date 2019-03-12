#ifndef LLVM_LIBERTY_LOCALITY_REMED_H
#define LLVM_LIBERTY_LOCALITY_REMED_H

#include "llvm/IR/DataLayout.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Utilities/GetMemOper.h"
//#include "liberty/Utilities/GetSize.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Orchestration/LocalityAA.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Classify.h"


namespace liberty
{
using namespace llvm;

class LocalityRemedy : public Remedy {
public:
  // application of separation logic requires 3 checks for misspec:
  // 1) ensure separation to the 5 families
  // 2) guard all accesses to private objects with private_read and
  // private_store function calls
  // 3) ensure that short-lived objects are not live at the end of iteration

  // TODO: eventually need to keep the private and local AUs (of type Ptrs
  // defined in Pieces.h). Also need to keep all the insts involved in private
  // accesses. These insts need to be guarded
  // TODO: possibly need split to multiple separated remedies. For example,
  // separation cost maybe should be separated from cost for private objects.
  // remedies maybe need to also record all the AUs assumed to be part of a
  // family. Heap assignment might be adjusted based on the selected remedies.
  const Instruction *privateI;
  const Instruction *localI;

  void apply(PDG &pdg);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "locality-remedy"; };
};

class LocalityRemediator : public Remediator {
public:
  LocalityRemediator(const Read &rd, const HeapAssignment &c, Pass &p)
      : read(rd), asgn(c), proxy(p) {
    aa = nullptr;
  }

  StringRef getRemediatorName() const { return "locality-remediator"; }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   bool RAW, const Loop *L);

private:
  const Read &read;
  const HeapAssignment &asgn;
  LoopAA *aa;
  Pass &proxy;
};

} // namespace liberty

#endif
