#include "internals/constants.h"
#include "internals/sw_queue/sw_queue.h"

namespace specpriv_smtx
{

typedef struct
{
  queue_t** queues;
  uint32_t  n_queues;
} PREFIX(queue);

}
