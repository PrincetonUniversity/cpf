#define DEBUG_TYPE "capture-util"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/CaptureUtil.h"

#include <set>

using namespace llvm;
using namespace liberty;

// This is a list of functions which do not capture their arguments.
static StringRef  nonCaptureFunctions[] = {
#include "NonCaptureFunctions.h"
""
};

typedef std::set<const Value*> VisitedSet;

static bool findAllCapturesRec(const Value *v,
                               VisitedSet &visited,
                               CaptureSet *captureSet);

static bool captures(const Value *v, const Value *use,
                     VisitedSet &visited, CaptureSet *captureSet) {

  const Instruction *inst = dyn_cast<Instruction>(use);
  if(!inst)
    return findAllCapturesRec(use, visited, captureSet);

  if(const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(inst)) {

    if(gep->getPointerOperand() == v)
      return findAllCapturesRec(gep, visited, captureSet);

    if(captureSet) captureSet->insert(use);
    return true;
  }

  if(isa<BitCastInst>(inst))
    return findAllCapturesRec(inst, visited, captureSet);

  if(const StoreInst *store = dyn_cast<StoreInst>(inst)) {

    if(store->getValueOperand() == v) {
      if(captureSet) captureSet->insert(use);
      return true;
    }

    return false;
  }

  if(isa<LoadInst>(inst))
    return false;

  if(isa<CmpInst>(inst))
    return false;

  if( const PHINode *phi = dyn_cast<PHINode>(inst) )
    return findAllCapturesRec(phi, visited, captureSet);

  CallSite CS = getCallSite(const_cast<Instruction *>(inst));
  if(!CS.getInstruction()) {
    if(captureSet) captureSet->insert(use);
    return true;
  }

  const Function *f = CS.getCalledFunction();
  if(!f) {
    if(captureSet) captureSet->insert(use);
    return true;
  }

  for(unsigned j=0; nonCaptureFunctions[j].empty(); ++j)
    if( f->getName().equals( nonCaptureFunctions[j] ) )
      return false;

  if(CS.getCalledValue() == use) {
    if(captureSet) captureSet->insert(use);
    return true;
  }

  for(unsigned i = 0; i < CS.arg_size(); ++i) {
    if(CS.getArgument(i) == v &&
       ( i + 1 == CS.arg_size()  ||
       (!CS.paramHasAttr(i + 1, Attribute::NoCapture) &&
       !CS.paramHasAttr(i + 1, Attribute::ByVal)))) {

      Function::const_arg_iterator arg = f->arg_begin();
      for(unsigned j = 0; j < i; ++j) ++arg;

      // Don't include these instructions in the captureSet
      if(f->isVarArg() || f->isDeclaration() ||
         findAllCapturesRec(arg, visited, NULL)) {
         const Module *M = f->getParent();
         const DataLayout &DL = M->getDataLayout();
        DEBUG(errs()
              << "Captured " << *GetUnderlyingObject(v, DL)
              << " by call to '" << f->getName()
              << "'\n");

        if(captureSet) captureSet->insert(use);
        return true;
      }
    }
  }

  return false;
}

static bool findAllCapturesRec(const Value *v,
                               VisitedSet &visited,
                               CaptureSet *captureSet) {

  if(!visited.count(v)) {

    visited.insert(v);

    typedef Value::const_user_iterator UseIt;
    for(UseIt use = v->user_begin(); use != v->user_end(); ++use) {

      bool isCaptured = captures(v, *use, visited, captureSet);
      if(isCaptured && !captureSet)
        return true;
    }
  }

  if(!captureSet)
    return false;

  return captureSet->size() > 0;
}


bool liberty::findAllCaptures(const Value *v, CaptureSet *captureSet) {
  VisitedSet visited;
  return findAllCapturesRec(v, visited, captureSet);
}
