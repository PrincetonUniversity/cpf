#define DEBUG_TYPE "exclusions"

#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Exclusions/Exclusions.h"


namespace liberty {

  //STATISTIC(numExclusions, "Number of fcns created by MTCG and friends");

  using namespace llvm;

  char Exclusions::ID = 0;
  namespace {
    static RegisterPass<Exclusions> RP("exclusions", "A pass that maintains a registry of exclusions",
                    false, false);
  }

  // A set of functions to exclude
  typedef DenseSet<const llvm::Function*>                         FcnSet;
  static FcnSet                                             exclusions;

  void Exclusions::insert(const Function *f) {
    //++numExclusions;
    exclusions.insert(f);
  }

  bool Exclusions::exclude(const Function *f) const {
    return exclusions.count(f) != 0;
  }

  void Exclusions::dump() const {
    LLVM_LLVM_DEBUG(errs() << "Exclusions size is " << exclusions.size() << ".\n");
    for(FcnSet::iterator i=exclusions.begin(), e=exclusions.end(); i!=e; ++i) {
      LLVM_LLVM_DEBUG(errs() << "\t" << (*i)->getName() << "\n");
    }
  }

  void Exclusions::reset() {
    exclusions.clear();
  }
}
