#ifndef LLVM_LIBERTY_SPLIT_FCN_H
#define LLVM_LIBERTY_SPLIT_FCN_H

#include  "Predicate.h"

namespace llvm
{
  class Function;
  class BasicBlock;
}

namespace liberty
{
  using namespace llvm;

  /// Given a predicate over basic blocks,
  /// if there are paths through fcn for which all blocks
  /// satisfy the predicate, but there are other blocks
  /// which do not, then split this into two functions:
  /// one 'safe' and one 'unsafe'.  The unsafe function
  /// will only contain paths which satisfy isSafe.
  /// Additionally, update all callsites of fcn so they
  /// instead try to call 'safe' and then call 'unsafe'.
  bool splitFunction(Function *fcn, Predicate<BasicBlock> &isSafe);
}


#endif

