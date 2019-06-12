#ifndef LLVM_LIBERTY_SPEC_PRIV_QUEUE_H
#define LLVM_LIBERTY_SPEC_PRIV_QUEUE_H

#include "constants.h"
#include "sw_queue.h"

typedef struct
{
  queue_t** queues;
  uint32_t  n_queues;
} PREFIX(queue);

#endif
