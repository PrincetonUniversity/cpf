#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "q.h"

struct s_queuelet
{
  sem_t             num_empty_slots;
  sem_t             num_full_slots;
  pthread_mutex_t   lock;

  Event **          slots;
  int               put;
  int               pull;
};
typedef struct s_queuelet Queuelet;

struct s_queue
{
  Queuelet          queuelets[QLETS_PER_Q];
};



static void gg_init_queuelet(Queuelet *q)
{
  sem_init( & q->num_empty_slots, 0, N_SLOTS );
  sem_init( & q->num_full_slots,  0, 0 );
  pthread_mutex_init( & q->lock, 0 );

  q->slots = (Event**) malloc( N_SLOTS * sizeof(Event*) );
  memset(q->slots, 0, N_SLOTS * sizeof(Event*));
  q->put = q->pull = 0;
}

static void gg_destroy_queuelet(Queuelet *q)
{
#if DEBUG_LEVEL(1)
  fprintf(stderr, "Free queue.\n");
  memset( q->slots, 0xaa, N_SLOTS*sizeof(Event*));
#endif
  free( q->slots );
  pthread_mutex_destroy( &q->lock );
  sem_destroy( &q->num_full_slots );
  sem_destroy( &q->num_empty_slots );
}

Queue *gg_new_queue(void)
{
  Queue *q = (Queue*)malloc( sizeof(Queue) );
  for(unsigned i=0; i<QLETS_PER_Q; ++i)
    gg_init_queuelet( &q->queuelets[i] );

  return q;
}

void gg_free_queue(Queue *q)
{
  for(unsigned i=0; i<QLETS_PER_Q; ++i)
    gg_destroy_queuelet( &q->queuelets[i] );
}

void gg_flush_queue(Queue *q)
{
}

static void gg_queuelet_push(Queuelet *q, Event *evt)
{
#if DEBUG_LEVEL(1)
  fprintf(stderr, "push %ld\n", (uint64_t)evt);
#endif

  // Wait until there is an empty slot
  while( 0 != sem_wait( &q->num_empty_slots ) )
  {
    // sem_wait can be interrupted
  }

  pthread_mutex_lock( &q->lock );
  {
    const int put = q->put;
    q->slots[ put ] = evt;
    q->put = (put + 1) % N_SLOTS;
  }
  pthread_mutex_unlock( &q->lock );

  // Announce that there is work in queue
  sem_post( &q->num_full_slots );
}

#if QLETS_PER_Q > 1
static uint64_t hash(Event *evt)
{
  uint64_t p = (uint64_t)evt;
  return (p>>4) ^ (p<<(64-4));
}
#endif

void gg_queue_push(Queue *q, Event *evt)
{
#if QLETS_PER_Q > 1
  const unsigned ql = hash(evt) % QLETS_PER_Q;
#else
  const unsigned ql = 0;
#endif

  gg_queuelet_push( &q->queuelets[ql], evt );
}

static Event *gg_queuelet_trypop(Queuelet *q)
{
  Event *result = 0;

  // If work is available.
#if QLETS_PER_Q > 1
  if( sem_trywait( &q->num_full_slots ) == -1 )
    return 0;
#else
  while( 0 != sem_wait( &q->num_full_slots ) )
  {
    // sem wait can be interrupted.
  }
#endif

  pthread_mutex_lock( &q->lock );
  {
    const int pull = q->pull;
    result = q->slots[ pull ];
#if DEBUG_LEVEL(1)
    q->slots[ pull ] = 0;
#endif
    q->pull = (pull + 1) % N_SLOTS;
  }
  pthread_mutex_unlock( &q->lock );

  // Announce that the queue has space.
  sem_post( &q->num_empty_slots );

#if DEBUG_LEVEL(1)
  fprintf(stderr, "pop %ld\n", (uint64_t)result);
#endif

  return result;
}

Event *gg_queue_pop(Queue *q)
{
#if QLETS_PER_Q > 1
  static unsigned i=0;
  for(;;)
  {
    i = (i+1) % QLETS_PER_Q;
    Event *e = gg_queuelet_trypop( &q->queuelets[i] );

    if( e )
      return e;
  }
#else
  return gg_queuelet_trypop( &q->queuelets[0] );
#endif
}




