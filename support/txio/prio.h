#ifndef LIBERTY_PUREIO_PRIORITY_QUEUE_H
#define LIBERTY_PUREIO_PRIORITY_QUEUE_H

#include <stdint.h>

#include "types.h"

//------------------------------------------------------------------------
// Methods for manipulating priority queues of (time vector -> transaction)
//
// We use a simple binary heap for our priority queue.

struct s_prio_queue
{
  // Binary heap of suspended operations,
  // ordered by their epoch, ascending.
  Event              ** ops;
  uint32_t              capacity;
  uint32_t              length;
};
typedef struct s_prio_queue PrioQueue;

void destruct_q(PrioQueue *pq);

void maybe_grow(PrioQueue *pq);

void init_q(PrioQueue *pq);

uint32_t size_q(PrioQueue *pq);

void insert_evt(PrioQueue *pq, Event *tx);

Event *findmin_evt(PrioQueue *pq);

Event *removemin_evt(PrioQueue *pq);

void remove_from_q(PrioQueue *pq, Event *evt);

int queue_contains(PrioQueue *pq, Event *evt);

#endif

