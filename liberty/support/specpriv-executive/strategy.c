#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>

#include "api.h"
#include "constants.h"
#include "strategy.h"

static uint32_t n_loops; // # of parallelized loops

unsigned  __specpriv__curLoop_num_stages = 0;         // number of worker threads for each stage
unsigned* __specpriv__curLoop_stage2rep = NULL;
unsigned* __specpriv__curLoop_wid2stage = NULL;       // map between worker id and corresponding stage
unsigned* __specpriv__curLoop_stage2firstwid = NULL;  // for each stage, holds smallest wid that assigned to the stage


unsigned*  __specpriv__num_stages = NULL;
unsigned** __specpriv__stage2rep = NULL;
unsigned** __specpriv__wid2stage = NULL;
unsigned** __specpriv__stage2firstwid = NULL;

void PREFIX(alloc_strategies_info)(uint32_t nl)
{
  n_loops = nl;
  __specpriv__num_stages = (unsigned *)malloc(sizeof(unsigned) * n_loops);
  __specpriv__stage2rep = (unsigned **)malloc(sizeof(unsigned *) * n_loops);
  __specpriv__wid2stage = (unsigned **)malloc(sizeof(unsigned *) * n_loops);
  __specpriv__stage2firstwid =
      (unsigned **)malloc(sizeof(unsigned *) * n_loops);
}

void PREFIX(inform_strategy)(unsigned num_workers, unsigned loopID, unsigned num_stages, ...)
{
  unsigned i, j;

  __specpriv__num_stages[loopID] = num_stages;

  // initialize stage2rep

  __specpriv__stage2rep[loopID] = (unsigned*)malloc( sizeof(unsigned) * num_stages );

  va_list ap;
  va_start(ap, num_stages);

  for (i = 0 ; i < num_stages ; i++)
    __specpriv__stage2rep[loopID][i] = va_arg(ap, unsigned);

  va_end(ap);

  // initialize wid2stage and stage2firstwid

  __specpriv__wid2stage[loopID] = (unsigned*)malloc( sizeof(unsigned) * num_workers );
  __specpriv__stage2firstwid[loopID] = (unsigned*)malloc( sizeof(unsigned) * num_workers );

  unsigned  wid = 0;
  for (i = 0 ; i < num_stages ; i++)
  {
    __specpriv__stage2firstwid[loopID][i] = wid;
    for (j = 0 ; j < __specpriv__stage2rep[loopID][i] ; j++)
      __specpriv__wid2stage[loopID][wid++] = i;
  }

  assert(wid == num_workers);
}

void PREFIX(set_current_loop_strategy)(unsigned loopID)
{
  __specpriv__curLoop_num_stages = __specpriv__num_stages[loopID];
  __specpriv__curLoop_stage2rep = __specpriv__stage2rep[loopID];
  __specpriv__curLoop_wid2stage = __specpriv__wid2stage[loopID];
  __specpriv__curLoop_stage2firstwid = __specpriv__stage2firstwid[loopID];
}

void PREFIX(cleanup_strategy)(void)
{
  for (unsigned i = 0; i < n_loops; ++i) {
    free(__specpriv__stage2rep[i]);
    free(__specpriv__wid2stage[i]);
    free(__specpriv__stage2firstwid[i]);
  }
  free(__specpriv__num_stages);
  free(__specpriv__stage2rep);
  free(__specpriv__wid2stage);
  free(__specpriv__stage2firstwid);
}
