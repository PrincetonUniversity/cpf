#ifndef EXCLUSIONS_H
#define EXCLUSIONS_H

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

// A registry of functions that should be excluded.

namespace liberty {

  using namespace llvm;


  class Exclusions : public ImmutablePass {
    public:
      static char ID;
      Exclusions() : ImmutablePass(ID) {}

      void insert(const Function *f);
      bool exclude(const Function *f) const;

      void dump() const;
      void reset();
  };

}

#endif

