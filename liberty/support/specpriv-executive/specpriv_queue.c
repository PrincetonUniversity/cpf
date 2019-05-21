#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include "specpriv_queue.h"
#include "api.h"
#include "debug.h"
#include "strategy.h"
#include "types.h"
#include "constants.h"
#include "config.h"

typedef union _box
{
  int64_t i;
  double  d;
} box;

static __specpriv_queue ***specpriv_queues;
static uint32_t n_loops; // # of parallelized loops
static uint32_t *n_stageQs;

static queue_t* get_queue(__specpriv_queue* specpriv_queue)
{
  unsigned index = (unsigned)__specpriv_current_iter() % (specpriv_queue->n_queues);
  queue_t* queue = specpriv_queue->queues[index];
  return queue;
}

// ---------------------------------------------------------
// queue APIs
//

void __specpriv_alloc_queues(uint32_t nl)
{
  n_loops = nl;
  specpriv_queues = (__specpriv_queue ***)mmap(
      0, sizeof(__specpriv_queue **) * n_loops, PROT_WRITE | PROT_READ,
      MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  n_stageQs = (uint32_t *) malloc (sizeof(uint32_t) * n_loops);
}

void  __specpriv_alloc_stage_queues(uint32_t n_stageQ, uint32_t loopID)
{
  specpriv_queues[loopID] = (__specpriv_queue **)mmap(
      0, sizeof(__specpriv_queue *) * n_stageQ, PROT_WRITE | PROT_READ,
      MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  n_stageQs[loopID] = n_stageQ;
}

void __specpriv_create_queue(uint32_t N, uint32_t M, uint32_t loopID, uint32_t qID)
{
  DBG("__specpriv_create_queue: %u x %u\n", N, M);

  // TODO: precise behavior of N-to-M queue is unclear

  assert(N == 1 || M == 1);

  unsigned i;
  unsigned n_queues = N*M;

  specpriv_queues[loopID][qID] = (__specpriv_queue*)mmap(0, sizeof(__specpriv_queue),
      PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  __specpriv_queue* specpriv_queue = specpriv_queues[loopID][qID];

  specpriv_queue->queues = (queue_t**)mmap(0, sizeof(queue_t*)*n_queues,
      PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  for (i = 0 ; i < n_queues; i++)
    specpriv_queue->queues[i] = __sw_queue_create();
  specpriv_queue->n_queues = n_queues;

  DBG("__specpriv_create_queue: %u x %u queue created successfully, %p\n", N, M, specpriv_queue);
}

__specpriv_queue* __specpriv_fetch_queue(uint32_t loopID, uint32_t qID)
{
  return specpriv_queues[loopID][qID];
}

void __specpriv_produce(__specpriv_queue* specpriv_queue, int64_t value)
{
  box b;
  b.i = value;
  DBG("__specpriv_produce %p: %lx ( or %lf )\n", specpriv_queue, value, b.d);

  __sw_queue_produce( get_queue(specpriv_queue), (void*)value );
}

void __specpriv_produce_replicated(__specpriv_queue* specpriv_queue, int64_t value)
{
  box b;
  b.i = value;
  DBG("__specpriv_produce_replicated %p: %lx ( or %lf )\n", specpriv_queue, value, b.d);

  unsigned i = 0;
  for ( ; i < specpriv_queue->n_queues ; i++)
    __sw_queue_produce( specpriv_queue->queues[i], (void*)value );
}

int64_t __specpriv_consume(__specpriv_queue* specpriv_queue)
{
  DBG("__specpriv_consume %p: ", specpriv_queue);
  int64_t ret = (int64_t)__sw_queue_consume( get_queue(specpriv_queue) );

  box b;
  b.i = ret;
  DBG("%lx ( or %lf )\n", ret, b.d);

  return ret;
}

int64_t __specpriv_consume_replicated(__specpriv_queue* specpriv_queue)
{
  DBG("__specpriv_consume_replicated %p: ", specpriv_queue);

  Wid      wid = __specpriv_my_worker_id();
  unsigned my_stage = GET_MY_STAGE(wid);
  Wid      index = wid - GET_FIRST_WID_OF_STAGE(my_stage);

  int64_t ret = (int64_t)__sw_queue_consume( specpriv_queue->queues[index] );

  box b;
  b.i = ret;
  DBG("%lx ( or %lf )\n", ret, b.d);

  return ret;
}

void __specpriv_flush(__specpriv_queue* specpriv_queue)
{
  unsigned i = 0;
  for ( ; i < specpriv_queue->n_queues ; i++)
    __sw_queue_flush(specpriv_queue->queues[i]);
}

void __specpriv_clear(__specpriv_queue* specpriv_queue)
{
  Wid      wid = __specpriv_my_worker_id();
  unsigned my_stage = GET_MY_STAGE(wid);
  Wid      index = wid - GET_FIRST_WID_OF_STAGE(my_stage);

  __sw_queue_clear(specpriv_queue->queues[index]);
}

void __specpriv_reset_queue(__specpriv_queue* specpriv_queue)
{
  unsigned i = 0;
  for ( ; i < specpriv_queue->n_queues ; i++)
    __sw_queue_reset(specpriv_queue->queues[i]);
}

void __specpriv_free_queue(__specpriv_queue* specpriv_queue)
{
  DBG("__specpriv_free_queue: %p\n", specpriv_queue);

  unsigned i = 0;
  for ( ; i < specpriv_queue->n_queues ; i++)
    __sw_queue_free(specpriv_queue->queues[i]);

  munmap(specpriv_queue->queues, sizeof(queue_t*)*(specpriv_queue->n_queues));
  munmap(specpriv_queue, sizeof(__specpriv_queue));
}

void __specpriv_free_queues(void) {
  for (unsigned i = 0; i < n_loops; ++i) {
    for (unsigned j = 0; j < n_stageQs[i]; ++j) {
      __specpriv_free_queue(specpriv_queues[i][j]);
    }

    munmap(specpriv_queues[i], sizeof(__specpriv_queue *) * n_stageQs[i]);
  }

  free(n_stageQs);

  munmap(specpriv_queues, sizeof(__specpriv_queue **) * n_loops);
}
