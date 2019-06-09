#ifndef LLVM_LIBERTY_LOCALITY_REMED_H
#define LLVM_LIBERTY_LOCALITY_REMED_H

#include "llvm/IR/DataLayout.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"
#include "liberty/Utilities/SplitEdge.h"

//#include "liberty/Utilities/GetSize.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Orchestration/LocalityAA.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Api.h"
#include "liberty/PointsToProfiler/Indeterminate.h"

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
  Instruction *privateI;
  StoreInst *reduxS;
  //const Instruction *localI;

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
    Separated,
    Subheaps,
    LocalityAA
  };

  LocalityRemedType type;

  /*
  const Read *read;
  const HeapAssignment *asgn;
  std::set<const Value*> *alreadyInstrumented;
  Module *mod;
  Type *voidty, *voidptr;
  IntegerType *u8, *u32;
  */

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
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
    case Separated:
      return "locality-separated-remedy";
      break;
    case Subheaps:
      return "locality-subheaps-remedy";
      break;
    case LocalityAA:
      return "locality-aa-remedy";
      break;
    }
  };

  bool addUOCheck(Value *ptr);
  void insertUOCheck(Value *obj, HeapAssignment::Type heap);
};

class LocalityRemediator : public Remediator {
public:
  LocalityRemediator(const Read &rd, const HeapAssignment &c, Pass &p)
      : read(rd), asgn(c), proxy(p) {
    //localityaa = std::make_unique<LocalityAA>(read, asgn);
    //localityaa = new LocalityAA(read, asgn);
  }

  /*
  ~LocalityRemediator() {
    delete localityaa;
  }
  */

  StringRef getRemediatorName() const { return "locality-remediator"; }

  Remedies satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms);

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried,
                   bool RAW, const Loop *L);

private:
  const Read &read;
  const HeapAssignment &asgn;
  Pass &proxy;
  //std::unique_ptr<LocalityAA> localityaa;
  LocalityAA *localityaa;
  //VSet alreadyInstrumented;

  unordered_set<const Instruction *> privateInsts;
};

} // namespace liberty

#endif
