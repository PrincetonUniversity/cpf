#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include "api.h"
#include "internals/debug.h"
#include "internals/strategy.h"
#include "internals/private.h"

namespace specpriv_smtx
{

typedef union _box
{
  int64_t i;
  double  d;
} box;

static queue_t* get_queue(PREFIX(queue)* specpriv_queue)
{
  unsigned index = (unsigned)PREFIX(current_iter)() % (specpriv_queue->n_queues);
  queue_t* queue = specpriv_queue->queues[index];
  return queue;
}

// ---------------------------------------------------------
// queue APIs 
//

PREFIX(queue)* PREFIX(create_queue)(uint32_t N, uint32_t M)
{
  DBG("__specpriv_create_queue: %u x %u\n", N, M);

  // TODO: precise behavior of N-to-M queue is unclear

  assert(N == 1 || M == 1);

  unsigned i;
  unsigned n_queues = N*M;

  PREFIX(queue)* specpriv_queue = (PREFIX(queue)*)mmap(0, sizeof(PREFIX(queue)), 
      PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  specpriv_queue->queues = (queue_t**)mmap(0, sizeof(queue_t*)*n_queues,
      PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  for (i = 0 ; i < n_queues; i++)
    specpriv_queue->queues[i] = __sw_queue_create();
  specpriv_queue->n_queues = n_queues;

  DBG("__specpriv_create_queue: %u x %u queue created successfully, %p\n", N, M, specpriv_queue);

  return specpriv_queue;
}

void PREFIX(produce)(PREFIX(queue)* specpriv_queue, int64_t value)
{
  box b;
  b.i = value;
  DBG("__specpriv_produce %p: %lx ( or %lf )\n", specpriv_queue, value, b.d);

  __sw_queue_produce( get_queue(specpriv_queue), (void*)value );
}

void PREFIX(produce_replicated)(PREFIX(queue)* specpriv_queue, int64_t value)
{
  box b;
  b.i = value;
  DBG("__specpriv_produce_replicated %p: %lx ( or %lf )\n", specpriv_queue, value, b.d);

  unsigned i = 0;
  for ( ; i < specpriv_queue->n_queues ; i++)
    __sw_queue_produce( specpriv_queue->queues[i], (void*)value );
}

int64_t PREFIX(consume)(PREFIX(queue)* specpriv_queue)
{
  DBG("__specpriv_consume %p: ", specpriv_queue);
  int64_t ret = (int64_t)__sw_queue_consume( get_queue(specpriv_queue) );

  box b;
  b.i = ret;
  DBG("%lx ( or %lf )\n", ret, b.d);

  return ret;
}

int64_t PREFIX(consume_replicated)(PREFIX(queue)* specpriv_queue)
{
  DBG("__specpriv_consume_replicated %p: ", specpriv_queue);
  
  Wid      wid = PREFIX(my_worker_id)();
  unsigned my_stage = GET_MY_STAGE(wid);
  Wid      index = wid - GET_FIRST_WID_OF_STAGE(my_stage);

  int64_t ret = (int64_t)__sw_queue_consume( specpriv_queue->queues[index] );

  box b;
  b.i = ret;
  DBG("%lx ( or %lf )\n", ret, b.d);

  return ret;
}

void PREFIX(flush)(PREFIX(queue)* specpriv_queue)
{
  unsigned i = 0;
  for ( ; i < specpriv_queue->n_queues ; i++)
    __sw_queue_flush(specpriv_queue->queues[i]);
}

void PREFIX(clear)(PREFIX(queue)* specpriv_queue)
{
  Wid      wid = PREFIX(my_worker_id)();
  unsigned my_stage = GET_MY_STAGE(wid);
  Wid      index = wid - GET_FIRST_WID_OF_STAGE(my_stage);

  __sw_queue_clear(specpriv_queue->queues[index]);
}

void PREFIX(reset_queue)(PREFIX(queue)* specpriv_queue)
{
  unsigned i = 0;
  for ( ; i < specpriv_queue->n_queues ; i++)
    __sw_queue_reset(specpriv_queue->queues[i]);
}

void PREFIX(free_queue)(PREFIX(queue)* specpriv_queue)
{
  DBG("__specpriv_free_queue: %p\n", specpriv_queue);

  unsigned i = 0;
  for ( ; i < specpriv_queue->n_queues ; i++)
    __sw_queue_free(specpriv_queue->queues[i]);

  munmap(specpriv_queue->queues, sizeof(queue_t*)*(specpriv_queue->n_queues));
  munmap(specpriv_queue, sizeof(PREFIX(queue)));
}

}
