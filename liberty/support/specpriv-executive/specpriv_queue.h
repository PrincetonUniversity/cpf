#ifndef LLVM_LIBERTY_SPEC_PRIV_QUEUE_H
#define LLVM_LIBERTY_SPEC_PRIV_QUEUE_H

#include "constants.h"
#include "sw_queue.h"

typedef struct
{
  queue_t** queues;
  uint32_t  n_queues;
} PREFIX(queue);

// gc14: couldn't think of a way of doing this more elegantly
void __specpriv_produce(__specpriv_queue* specpriv_queue, int64_t value);
int64_t __specpriv_consume(__specpriv_queue* specpriv_queue);

#endif
