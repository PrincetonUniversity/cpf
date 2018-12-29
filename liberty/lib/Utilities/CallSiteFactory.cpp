#include "liberty/Utilities/CallSiteFactory.h"

using namespace llvm;

CallSite liberty::getCallSite(Value *value) {
  return CallSite(value);
}

const CallSite liberty::getCallSite(const Value *value) {
  return getCallSite(const_cast<Value *>(value));
}

