#define DEBUG_TYPE "commlib"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/SpecPriv/CommutativeLibsRemed.h"

#define DEFAULT_COMM_LIBS_REMED_COST 100

namespace liberty
{
using namespace llvm;

STATISTIC(numQueries, "Num mem queries passed to comm libs");
STATISTIC(numMemDepRemoved, "Num mem deps from comm libs");
STATISTIC(numFunCallsMemDepRemoved,
          "Num mem deps removed from comm libs related "
          "to self commutative function calls");
STATISTIC(numRegQueries, "Num register deps queries");
STATISTIC(numRegDepRemoved, "Num register deps removed");

// set of functions that are usually considered commutative
// only address self commutative for now ( TODO: could also create CommSets or
// assume that some fun calls are pure)
const std::unordered_set<std::string>
    CommutativeLibsRemediator::CommFunNamesSet{"malloc", "calloc",  "realloc",
                                               "free",   "xalloc",  "rand",
                                               "random", "lrand48", "drand48"};

void CommutativeLibsRemedy::apply(PDG &pdg) {
  // TODO: ask programmer. Programmer questions should be applied first before any transformation
}

bool CommutativeLibsRemedy::compare(const Remedy_ptr rhs) const {
  // not using dynamic cast to avoid using RTTI. Use of static is safe here
  // since we know that the two compared remedies are of the same subclass
  // (already ensured that remedies names match)
  //
  // std::shared_ptr<CommutativeLibsRemedy> commLibsRhs =
  //    std::dynamic_pointer_cast<CommutativeLibsRemedy>(rhs);
  // assert(commLibsRhs);
  std::shared_ptr<CommutativeLibsRemedy> commLibsRhs =
      std::static_pointer_cast<CommutativeLibsRemedy>(rhs);
  return (this->functionName.compare(commLibsRhs->functionName) == -1);
}


/*
TODO: add isMallocLike CHECK
bool isProbCommFun (Function *CalledFun) {
  if (CommFunNamesSet.count(CalledFun->getName().str()) ||  isMallocLIKE)
    return true;
  return false;
}
*/

Function *CommutativeLibsRemediator::getCalledFun(const Instruction *A) {
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

Remediator::RemedResp CommutativeLibsRemediator::memdep(const Instruction *A,
                                                        const Instruction *B,
                                                        const bool LoopCarried,
                                                        const Loop *L) {

  ++numQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
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
    DEBUG(errs() << "Removed dep with commutative library identification. Dep "
                    "between function calls. Called function that was "
                    "considered commutative was "
                 << CalledFunA->getName() << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->functionName = CalledFunA->getName();
  }

  // check if deps across different iterations can be removed due to self
  // commutative functions
  if (LoopCarried) {
    const Function *FunA = A->getParent()->getParent();
    const Function *FunB = B->getParent()->getParent();

    if ((CalledFunA && CalledFunA == FunB &&
         CommFunNamesSet.count(FunB->getName().str()))) {
      ++numMemDepRemoved;
      DEBUG(
          errs() << "Removed dep with commutative library identification. Dep "
                    "between different iterations. Function that was "
                    "considered commutative was "
                 << CalledFunA->getName() << '\n');
      remedResp.depRes = DepResult::NoDep;
      remedy->functionName = CalledFunA->getName();
    } else if ((CalledFunB && CalledFunB == FunA &&
                CommFunNamesSet.count(FunA->getName().str())) ||
               (FunA == FunB && CommFunNamesSet.count(FunA->getName().str()))) {
      ++numMemDepRemoved;
      DEBUG(
          errs() << "Removed dep with commutative library identification. Dep "
                    "between different iterations. Function that was "
                    "considered commutative was "
                 << FunA->getName() << '\n');
      remedResp.depRes = DepResult::NoDep;
      remedy->functionName = FunA->getName();
    }
  }
  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedResp
CommutativeLibsRemediator::regdep(const Instruction *A, const Instruction *B,
                                  bool loopCarried, const Loop *L)

{
  ++numRegQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<CommutativeLibsRemedy> remedy =
      std::shared_ptr<CommutativeLibsRemedy>(new CommutativeLibsRemedy());
  remedy->cost = DEFAULT_COMM_LIBS_REMED_COST;

  // check if reg deps across different iterations can be removed due to self
  // commutative functions
  if (loopCarried) {
    Function *CalledFunA = getCalledFun(A);
    Function *CalledFunB = getCalledFun(B);

    const Function *FunA = A->getParent()->getParent();
    const Function *FunB = B->getParent()->getParent();

    if ((CalledFunA && CalledFunA == FunB &&
         CommFunNamesSet.count(FunB->getName().str()))) {
      ++numRegDepRemoved;
      DEBUG(errs()
            << "Removed reg dep with commutative library identification. Dep "
               "between different iterations. Function that was "
               "considered commutative was "
            << CalledFunA->getName() << '\n');
      remedResp.depRes = DepResult::NoDep;
      remedy->functionName = CalledFunA->getName();
    } else if ((CalledFunB && CalledFunB == FunA &&
                CommFunNamesSet.count(FunA->getName().str())) ||
               (FunA == FunB && CommFunNamesSet.count(FunA->getName().str()))) {
      ++numRegDepRemoved;
      DEBUG(errs()
            << "Removed reg dep with commutative library identification. Dep "
               "between different iterations. Function that was "
               "considered commutative was "
            << FunA->getName() << '\n');
      remedResp.depRes = DepResult::NoDep;
      remedy->functionName = FunA->getName();
    }
  }
  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
