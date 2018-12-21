#include <stdlib.h>

#include "config.h"
#include "tv.h"
#include "prio.h"
#include "event.h"



void destruct_q(PrioQueue *pq)
{
  if( pq->capacity > 0 )
    free( pq->ops );
  pq->capacity = pq->length = 0;
  pq->ops = 0;
}

void maybe_grow(PrioQueue *pq)
{
  // Possibly grow the heap.
  if( pq->length + 1 > pq->capacity )
  {
    unsigned new_capacity = 2 * pq->capacity;
    if( MIN_CAPACITY > new_capacity )
      new_capacity = MIN_CAPACITY;

    pq->ops = (Event**)
      realloc( pq->ops, sizeof(Event*) * new_capacity );

    pq->capacity = new_capacity;
  }
}

void init_q(PrioQueue *pq)
{
  pq->ops = 0;
  pq->capacity = pq->length = 0;
}

uint32_t size_q(PrioQueue *pq)
{
  return pq->length;
}

#if STATISTICS
static uint32_t max_length = 16;
#endif

void insert_evt(PrioQueue *pq, Event *tx)
{
  maybe_grow(pq);

  Event **heap = pq->ops;

  // Find the first free position of a leaf.
  unsigned pos = pq->length++;
  heap[ pos ] = tx;

  // Repeatedly swap with parent until it is greater than
  // its parent.
  while( pos > 0 )
  {
    unsigned parent = (pos-1)/2;

    Event *evtParent = heap[parent],
          *evtChild  = heap[pos];

    if( compare_tv_lte( evtParent->time, evtChild->time ) )
      break;

    heap[parent] = evtChild;
    heap[pos] = evtParent;

    pos = parent;
  }

#if STATISTICS
  if( pq->length > max_length )
  {
    max_length = pq->length;
    fprintf(stderr, "{{{%d}}}\n", max_length);
  }
#endif
}

Event *findmin_evt(PrioQueue *pq)
{
  return pq->ops[0];
}

static void remove_pos(PrioQueue *pq, unsigned pos)
{
  Event **heap = pq->ops;

#if DEBUG_LEVEL(1)
  // sanity guarantee
  heap[pos] = 0;
#endif

  if( --pq->length < 1 )
    return;

  Event *floater = heap[ pq->length ];
  while( pos < pq->length )
  {
    unsigned left  = 2*pos + 1,
             right = 2*pos + 2;

    // 3 cases: two children, only left child, no children.

    // This node has two children
    if( /* left < pq->length && */ right < pq->length )
    {
      Event *evtLeft  = heap[left],
            *evtRight = heap[right];

      // Find min of {float,left,right}:

      if( compare_tv_lte( floater->time, evtLeft->time ) )
      {
        if( compare_tv_lte( floater->time, evtRight->time ) )
        {
          // Floater < Left, Right
          heap[pos] = floater;
          break;
        }
        else
        {
          // Right < Floater < Left
          heap[pos] = evtRight;
          pos = right;
          continue;
        }
      }

      else
      {
        if( compare_tv_lte( evtLeft->time, evtRight->time ) )
        {
          // Left < Floater, Right
          heap[pos] = evtLeft;
          pos = left;
          continue;
        }
        else
        {
          // Right < Left < Floater
          heap[pos] = evtRight;
          pos = right;
          continue;
        }
      }
    }

    // This node has a left child, but no right child.
    else if( left < pq->length /* && right >= pq->length */ )
    {
      Event *evtLeft = heap[left];

      // Find min of {float,left}

      if( compare_tv_lte( floater->time, evtLeft->time ) )
      {
        // Floater < Left
        heap[pos] = floater;
        break;
      }
      else
      {
        // Left < Floater
        heap[pos] = evtLeft;
        pos = left;
        continue;
      }
    }

    // no children.
    else
    {
      heap[pos] = floater;
      break;
    }
  }
}

Event *removemin_evt(PrioQueue *pq)
{
  Event *evt = findmin_evt(pq);
  remove_pos(pq,0);
  return evt;
}

void remove_from_q(PrioQueue *pq, Event *evt)
{
  unsigned i;

  for(i=0; i<pq->length; ++i)
    if( pq->ops[i] == evt )
    {
      remove_pos(pq,i);
      return;
    }
}

int queue_contains(PrioQueue *pq, Event *evt)
{
  Event **heap = pq->ops;
  for(unsigned i=0; i<pq->length; ++i)
    if( heap[i] == evt )
      return 1;
  return 0;
}



