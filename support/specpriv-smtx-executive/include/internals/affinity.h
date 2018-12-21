#include "internals/constants.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

namespace specpriv_smtx
{

#if OS2PHYS
static const int oscpu2physcore[NUM_PROCS] =
  {
  /* P0: */ 0,  4,  5,  6,  7,  8,
  /* P1: */ 1,  9, 10, 11, 12, 13,
  /* P2: */ 2, 14, 15, 16, 17, 18,
  /* P3: */ 3, 19, 20, 21, 22, 23
  };
#define CORE(n)     ( oscpu2physcore[n] )
#else
#define CORE(n)     ( n )
#endif

extern int32_t num_procs;

}
