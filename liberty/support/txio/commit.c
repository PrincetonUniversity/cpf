#include <stdint.h>
#include <assert.h>

#include "config.h"
#include "commit.h"
#include "event.h"

#if !USE_COMMIT_THREAD
pthread_mutex_t the_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static int process_evt(Event *evt);

int dispatch_event(TX *tx, Event *evt)
{
#if USE_COMMIT_THREAD
  assert( tx->dispatch );
  gg_queue_push(tx->dispatch,evt);
  return 0;
#else
  pthread_mutex_lock( &the_lock );
  int rval = process_evt(evt);
  pthread_mutex_unlock( &the_lock );
  return rval;
#endif
}

void close_tx(TX *tx, uint32_t n, int blocking)
{
  assert( tx->base.op == SUB_TX );

  if( tx->total == n )
  {
    fprintf(stderr, "Double-close %d\n", n);
    return;
  }

  assert( tx->total == UNKNOWN  );

#if DEBUG_LEVEL(2)
  fprintf(stderr, "\t\t\t[[[Close: ");
  print_tx(tx);
  fprintf(stderr, "]]]\n");
#endif

  sem_t sync;
  Result result;
  if( blocking )
  {
#if DEBUG_LEVEL(2)
    fprintf(stderr, "Waiting for event to commit...\n");
#endif

    sem_init( &sync, 0, 0 );
    result.sync = &sync;
    tx->base.result = &result;
  }

  tx->not_total = n;
  dispatch_event(tx, &tx->base);

  if( blocking )
  {
#if USE_COMMIT_THREAD
    gg_flush_queue( tx->dispatch );
#endif

    while( 0 != sem_wait( &sync ) )
    { /* may be interrupted */ }

#if DEBUG_LEVEL(2)
    fprintf(stderr, "Event has committed\n");
#endif

    sem_destroy( &sync );
  }

}


Scalar issue(SusOp *sop, int blocking)
{
  assert( sop->base.op < N_SOP_TYPES );
  assert( sop->base.op != SUB_TX );

  TX *parent = sop->base.parent;
  assert(parent);
  assert(parent->base.op == SUB_TX);

  sem_t sync;
  Result result;
  if( blocking )
  {
    sem_init( &sync, 0, 0 );
    result.sync = &sync;
    sop->base.result = &result;
  }

  dispatch_event(parent,&sop->base);

  Scalar rval = {0};
  if( blocking )
  {
#if USE_COMMIT_THREAD
    gg_flush_queue( parent->dispatch );
#endif

#if DEBUG_LEVEL(2)
    fprintf(stderr, "\t\t\t[[[Wait]]]\n");
#endif
    while( 0 != sem_wait( &sync ) )
    { /* may be interrupted */ }

    rval = result.retval;
    sem_destroy( &sync );
  }

  return rval;
}



//------------------------------------------------------------------------
// Methods for progressive commit.

// The specified TX has changed...
static int progress(TX *tx)
{
  int root_is_done = 0;

#if DEBUG_LEVEL(3)
  fprintf(stderr, "\t\t\t[[[progress]]]\n");
#endif

#if DEBUG_LEVEL(2)
  fprintf(stderr, "Progress %ld\n", (uint64_t)tx);
#endif

  for(;;)
  {
#if DEBUG_LEVEL(2)
    fprintf(stderr, "..loop on %ld\n", (uint64_t)tx);
#endif
    assert(tx);
    assert(tx->base.op == SUB_TX);

#if DEBUG_LEVEL(3)
    fprintf(stderr, "\t\t\t\tAt ");
    print_tx(tx);
    fprintf(stderr, "\n");
#endif

    if( tx->total != UNKNOWN
    &&  tx->already >= tx->total )
    {
      assert( tx->already == tx->total && "Looks like a TX was closed with too-small a count!");
      TX *parent = tx->base.parent;

      // This TX is done.

      // If someone is listening, tell them so.
      Result *result = tx->base.result;
      if( result && result->sync )
        sem_post( result->sync );

      if( tx->in_parent_q && parent )
      {
        PrioQueue *pq = &parent->queue;
        if( findmin_evt(pq) == (Event*)tx )
        {
          free_tv( parent->upto );
          parent->upto = clone_time( tx->base.time );
        }
        remove_from_q(pq, (Event*) tx);
      }

      // Free the TX
      free_tx(tx);

      if( parent )
      {
        parent->already++;

        // We have finished this SUB-TX
        // continue on with the parent.
#if DEBUG_LEVEL(3)
        fprintf(stderr, "\t\t\t\t\tascend\n");
#endif
        tx = parent;
        continue;
      }

      else
      {
        // parent == null ==> root
        root_is_done = 1;
#if DEBUG_LEVEL(2)
        fprintf(stderr, "Root is done\n");
#endif
        break;
      }
    }

    if( tx->base.parent
    && !tx->in_parent_q
    &&  (tx->already < tx->total || tx->total == UNKNOWN) )
    {
      insert_evt( &tx->base.parent->queue, (Event*) tx );
      tx->in_parent_q = 1;
      tx = tx->base.parent;
      continue;
    }

    if( !tx->ready )
    {
#if DEBUG_LEVEL(2)
      fprintf(stderr, "\t\t\t\tnot ready\n");
#endif
      tx = tx->base.parent;
      if( tx->ready )
        continue;
      else
        break;//$$$
    }

    if( size_q( &tx->queue ) == 0 )
      break;

    Event *front = findmin_evt( &tx->queue );
#if DEBUG_LEVEL(2)
    fprintf(stderr, "\t\t\t\t\tfront: ");
    print_evt(front);
    fprintf(stderr, "\n");
#endif

    // The pigeonhole rule.
    if( tx->total != UNKNOWN
    &&  tx->total == tx->already + size_q( &tx->queue ) )
    {
#if DEBUG_LEVEL(2)
      fprintf(stderr, "\t\t\t\tPigeonhole\n");
#endif
      // We may execute an event
    }

    else if( tv_are_adjacent(tx->upto, front->time ) )
    {
#if DEBUG_LEVEL(2)
      fprintf(stderr, "\t\t\t\tAdjacency\n");
#endif
      // We may execute an event.
    }

    else
    {
#if DEBUG_LEVEL(3)
      fprintf(stderr, "\t\t\t\t\tno-go\n");
#endif
      break;
    }

    if( front->op == SUB_TX )
    {
#if DEBUG_LEVEL(3)
      fprintf(stderr, "\t\t\t\t\tdescend\n");
#endif

      TX *child = (TX*)front;

//      assert( !child->ready );
      child->ready = 1;

      tx = child;
      continue;
    }
    else
    {
      SusOp *sop = (SusOp*)front;

#if DEBUG_LEVEL(2)
      fprintf(stderr, "\t\t\t[[[  Run: ");
      print_sop( sop );
      fprintf(stderr, "\n");
#endif

      run_sop(sop);

      Event *removed = removemin_evt( &tx->queue );
      assert( front == removed );
#if DEBUG_LEVEL(1)
      assert( !queue_contains(&tx->queue,front) );
#endif

      free_tv( tx->upto );
      tx->upto = clone_time( front->time );

      // Optionally alert people that
      // the sop has run.
      Result *result = sop->base.result;
      if( result && result->sync )
      {
#if DEBUG_LEVEL(2)
        fprintf(stderr, "\t\t\t[[[Strobe]]]\n");
#endif
        sem_post( result->sync );
      }

      free_sop(sop);

      tx->already++;
    }
  }

  return root_is_done;
}

static int process_evt(Event *evt)
{
  if( evt->op == SUB_TX )
  {
    TX *tx = (TX*)evt;

    if( tx->total == UNKNOWN && tx->not_total != UNKNOWN )
      tx->total = tx->not_total;

#if DEBUG_LEVEL(1)
    fprintf(stderr, "Received event: ");
    print_tx(tx);
    fprintf(stderr, "\n");
#endif

    return progress(tx);
  }

  else
  {
#if DEBUG_LEVEL(1)
    fprintf(stderr, "Received event: ");
    print_sop((SusOp*)evt);
    fprintf(stderr,"\n");
#endif

    TX *parent = evt->parent;
    assert(parent);
#if DEBUG_LEVEL(1)
    assert( !queue_contains(&parent->queue, evt) );
#endif
    insert_evt( &parent->queue, evt );

    return progress(parent);
  }
}

#if USE_COMMIT_THREAD
void *commit_thread(void *arg)
{
#if DEBUG_LEVEL(2)
  fprintf(stderr, "Starting commit thread...\n");
#endif

  TX *root = (TX*)arg;
  Queue *q = root->dispatch;

  for(;;)
  {
    Event *evt = gg_queue_pop( q );
    assert(evt);
    assert( evt->op < N_SOP_TYPES );

    if( process_evt(evt) )
      break;
  }

  // shutdown
  gg_free_queue(q);

#if DEBUG_LEVEL(2)
  fprintf(stderr, "Commit thread shutdown\n");
#endif

  pthread_exit(0);
}
#endif


