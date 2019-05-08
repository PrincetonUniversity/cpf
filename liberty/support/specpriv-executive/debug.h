#ifndef LLVM_LIBERTY_SPEC_PRIV_DEBUG_H
#define LLVM_LIBERTY_SPEC_PRIV_DEBUG_H

#include <stdlib.h>

#include "constants.h"
#include "types.h"

void   init_debug(unsigned num_all_workers);
size_t DBG(const char* format, ...);
size_t PROFDUMP(const char* format, ...);

#endif
