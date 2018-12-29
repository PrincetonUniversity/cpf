#ifndef GLOBAL_CTORS_H
#define GLOBAL_CTORS_H

#include "llvm/IR/Function.h"

namespace liberty {
  using namespace llvm;

  // Add the function f to the global
  // constructors list (of its own module),
  // so that it is called before main().
  // Must be a function of 0-arity
  void callBeforeMain( Function *f, const unsigned int priority = 65535 );

  // Add the function f to the global
  // destructors list, so that it is
  // called after main()
  void callAfterMain( Function *f, const unsigned int priority = 65535 );
}

#endif

