#ifndef LIBERTY_PUREIO_Q_H
#define LIBERTY_PUREIO_Q_H

#include <pthread.h>
#include <semaphore.h>

#include "types.h"
#include "event.h"

//------------------------------------------------------------------------
// Methods for inter-procedural queues.
//
// This implementation is a semaphore-based bounded-buffer
// of events.  We have one queue and one commit thread per root-tx.

Queue *gg_new_queue(void);

void gg_free_queue(Queue *q);
void gg_flush_queue(Queue *q);
void gg_queue_push(Queue *q, Event *evt);
Event *gg_queue_pop(Queue *q);

#endif


