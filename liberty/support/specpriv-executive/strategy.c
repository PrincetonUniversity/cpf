#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>

#include "api.h"
#include "constants.h"
#include "strategy.h"

unsigned  __specpriv__num_stages = 0;
unsigned* __specpriv__stage2rep = NULL;       // number of worker threads for each stage
unsigned* __specpriv__wid2stage = NULL;       // map between worker id and corresponding stage
unsigned* __specpriv__stage2firstwid = NULL;  // for each stage, holds smallest wid that assigned to the stage

void PREFIX(inform_strategy)(unsigned num_workers, unsigned num_stages, ...)
{
  unsigned i, j;

  __specpriv__num_stages = num_stages;

  // initialize stage2rep

  __specpriv__stage2rep = (unsigned*)malloc( sizeof(unsigned) * num_stages );

  va_list ap;
  va_start(ap, num_stages);

  for (i = 0 ; i < num_stages ; i++)
    __specpriv__stage2rep[i] = va_arg(ap, unsigned);

  va_end(ap);

  // initialize wid2stage and stage2firstwid

  __specpriv__wid2stage = (unsigned*)malloc( sizeof(unsigned) * num_workers );
  __specpriv__stage2firstwid = (unsigned*)malloc( sizeof(unsigned) * num_workers );

  unsigned  wid = 0;
  for (i = 0 ; i < num_stages ; i++)
  {
    __specpriv__stage2firstwid[i] = wid;
    for (j = 0 ; j < __specpriv__stage2rep[i] ; j++)
      __specpriv__wid2stage[wid++] = i;
  }

  assert(wid == num_workers);
}

void PREFIX(cleanup_strategy)(void)
{
  free(__specpriv__stage2rep);
  free(__specpriv__wid2stage);
  free(__specpriv__stage2firstwid);
}
