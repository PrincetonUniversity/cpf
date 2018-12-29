#ifndef LAMP_FLAGS_H
#define LAMP_FLAGS_H

#include "llvm/Support/CommandLine.h"

// This header defines some command line options that are used by LAMP.

using namespace llvm;

extern cl::opt<bool> TLAMP;

//#define  CHECK_INST(instNum, lim) if(TLAMP && (lim.count((instNum)) == 0)  )
#define  CHECK_INST(inst, lim) if(TLAMP &&  !( (lim)->isLimiting(inst) ))
#define EX_CALL(inst)  \
  ( isa<CallInst>((inst)) && \
  ( (dyn_cast<CallInst>((inst))->getCalledFunction() == NULL) || \
    (dyn_cast<CallInst>((inst))->getCalledFunction()->isDeclaration())) \
    && !(isa< DbgInfoIntrinsic >((inst))))


#endif

