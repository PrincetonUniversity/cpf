#ifndef QUEUE_H
#define QUEUE_H

#include "ppool_shm_queue.h"

#define Queue SHM_Queue
#define produce(queue, value) shm_produce(queue, value)
#define flushQueue(queue) shm_flushQueue(queue)
#define consume(queue) shm_consume(queue)
#define createQueue(power) shm_createQueue(power)
#define freeQueue(queue) shm_freeQueue(queue)
#define emptyQueue(queue) shm_emptyQueue(queue)
#define produceChunk(queue, addr, size) shm_produceChunk(queue, addr, size)
#define consumeChunk(queue, addr, size) shm_consumeChunk(queue, addr, size)

#endif
