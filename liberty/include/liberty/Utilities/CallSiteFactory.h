#ifndef CALL_SITE_FACTORY
#define CALL_SITE_FACTORY

#include "llvm/IR/CallSite.h"

namespace liberty {
  llvm::CallSite getCallSite(llvm::Value *value);
  const llvm::CallSite getCallSite(const llvm::Value *value);
}

#endif /* CALL_SITE_FACTORY */
