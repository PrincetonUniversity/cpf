#define DEBUG_TYPE "commlib"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Orchestration/CommutativeLibsAA.h"

#ifndef DEFAULT_COMM_LIBS_REMED_COST
#define DEFAULT_COMM_LIBS_REMED_COST 100
#endif

namespace liberty
{
using namespace llvm;
using namespace llvm::noelle;

STATISTIC(numQueries, "Num mem queries passed to comm libs");
STATISTIC(numMemDepRemoved, "Num mem deps from comm libs");
STATISTIC(numFunCallsMemDepRemoved,
          "Num mem deps removed from comm libs related "
          "to self commutative function calls");
STATISTIC(numRegQueries, "Num register deps queries");
STATISTIC(numRegDepRemoved, "Num register deps removed");

bool CommutativeLibsRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<CommutativeLibsRemedy> commLibsRhs =
      std::static_pointer_cast<CommutativeLibsRemedy>(rhs);
  return (this->functionName.compare(commLibsRhs->functionName) == -1);
}

// set of functions that are usually considered commutative
// only address self commutative for now ( TODO: could also create CommSets or
// assume that some fun calls are pure)
const std::unordered_set<std::string> CommutativeLibsAA::CommFunNamesSet{
    "malloc", "calloc", "realloc", "free",   "xalloc",
    "rand",   "random", "lrand48", "drand48"};
/*
TODO: add isMallocLike CHECK
bool isProbCommFun (Function *CalledFun) {
  if (CommFunNamesSet.count(CalledFun->getName().str()) ||  isMallocLIKE)
    return true;
  return false;
}
*/

Function *CommutativeLibsAA::getCalledFun(const Instruction *A) {
  Function *FunA;
  const CallInst *call = dyn_cast<CallInst>(A);
  const InvokeInst *invoke = dyn_cast<InvokeInst>(A);
  if (call)
    FunA = call->getCalledFunction();
  else if (invoke)
    FunA = invoke->getCalledFunction();
  else
    FunA = nullptr;
  return FunA;
}

LoopAA::ModRefResult CommutativeLibsAA::modref(const Instruction *A,
                                               TemporalRelation rel,
                                               const Value *ptrB,
                                               unsigned sizeB, const Loop *L,
                                               Remedies &R) {

  return LoopAA::modref(A, rel, ptrB, sizeB, L, R);
}

LoopAA::ModRefResult CommutativeLibsAA::modref(const Instruction *A,
                                               TemporalRelation rel,
                                               const Instruction *B,
                                               const Loop *L, Remedies &R) {

  ++numQueries;

  std::shared_ptr<CommutativeLibsRemedy> remedy =
      std::shared_ptr<CommutativeLibsRemedy>(new CommutativeLibsRemedy());
  remedy->cost = DEFAULT_COMM_LIBS_REMED_COST;

  Function *CalledFunA = getCalledFun(A);
  Function *CalledFunB = getCalledFun(B);

  // remove deps between calls to self commutative functions (reflexive dep)
  if (CalledFunA && CalledFunA == CalledFunB &&
      CommFunNamesSet.count(CalledFunA->getName().str())) {
    ++numMemDepRemoved;
    ++numFunCallsMemDepRemoved;
    //LLVM_DEBUG(errs() << "Removed dep with commutative library identification. Dep "
   //                 "between function calls. Called function that was "
   ///                 "considered commutative was "
   //              << CalledFunA->getName() << '\n');
    remedy->functionName = CalledFunA->getName();
    R.insert(remedy);
    return NoModRef;
  }

  // check if deps across different iterations can be removed due to self
  // commutative functions
  if (rel != LoopAA::Same) {
    const Function *FunA = A->getParent()->getParent();
    const Function *FunB = B->getParent()->getParent();

    if ((CalledFunA && CalledFunA == FunB &&
         CommFunNamesSet.count(FunB->getName().str()))) {
      ++numMemDepRemoved;
      /*
      LLVM_DEBUG(
          errs() << "Removed dep with commutative library identification. Dep "
                    "between different iterations. Function that was "
                    "considered commutative was "
                 << CalledFunA->getName() << '\n');
      */
      remedy->functionName = CalledFunA->getName();
      R.insert(remedy);
      return NoModRef;
    } else if ((CalledFunB && CalledFunB == FunA &&
                CommFunNamesSet.count(FunA->getName().str())) ||
               (FunA == FunB && CommFunNamesSet.count(FunA->getName().str()))) {
      ++numMemDepRemoved;
      /*
      LLVM_DEBUG(
          errs() << "Removed dep with commutative library identification. Dep "
                    "between different iterations. Function that was "
                    "considered commutative was "
                 << FunA->getName() << '\n');
                 */
      remedy->functionName = FunA->getName();
      R.insert(remedy);
      return NoModRef;
    }
  }
  return LoopAA::modref(A, rel, B, L, R);
}

} // namespace liberty
