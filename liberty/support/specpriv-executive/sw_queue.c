#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "debug.h"
#include "sw_queue.h"

#define SW_QUEUE_OCCUPIED 1   // b'01
#define SW_QUEUE_NULL 0

#define SYNC_FINISHED 1
#define SYNC_NOT_FINISHED 0

queue_t* __sw_queue_create(void)
{
  queue_t* queue = (queue_t*)mmap(0, sizeof(queue_t),
      PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  __sw_queue_reset(queue);

  return queue;
}

void __sw_queue_produce(queue_t* queue, void* value)
{
  //DBG("\t\tsw_queue_produce, queue %p\n", queue);
  while (*(queue->head) != (void*) SW_QUEUE_NULL);

  *(queue->head+1) = (void*)value;

  *queue->head = (void *) SW_QUEUE_OCCUPIED;

  queue->head+=2;
  if ( queue->head >= &(queue->data[QUEUE_SIZE]) )
    queue->head = &queue->data[0];
}

void* __sw_queue_consume(queue_t* queue)
{
  void *value;
  while ((*queue->tail) == (void*) SW_QUEUE_NULL);

  value = *( (queue->tail)+1 );
  *( (queue->tail)+1 ) = (void*) SW_QUEUE_NULL;
  *(queue->tail) = (void*) *( (queue->tail)+1 );

  //DBG("\t\tsw_queue_consume, from tail %p\n", queue->tail);

  queue->tail+=2;
  if ( queue->tail >= &(queue->data[QUEUE_SIZE]) )
    queue->tail = &(queue->data[0]);

  return value;
}

void __sw_queue_flush(queue_t* queue)
{
  // As the queue does not have a buffer, nothing to do with flush
  /*
  while((*queue->tail) != (void*) SW_QUEUE_NULL)
  {
    (*queue->tail) = (void*) SW_QUEUE_NULL;
    queue->tail+=2;
    if ( queue->tail >= &(queue->data[QUEUE_SIZE]) )
      queue->tail = &queue->data[0];
  }
  */
}

void __sw_queue_clear(queue_t* queue)
{
  while((*queue->tail) != (void*) SW_QUEUE_NULL)
  {
    (*queue->tail) = (void*) SW_QUEUE_NULL;
    queue->tail+=2;
    if ( queue->tail >= &(queue->data[QUEUE_SIZE]) )
      queue->tail = &queue->data[0];
  }
}

void __sw_queue_reset(queue_t* queue)
{
  int i;
  queue->head = &(queue->data[0]);
  queue->tail = &(queue->data[0]);
  *(queue->finished) = SYNC_NOT_FINISHED;
  for(i=0 ; i<QUEUE_SIZE ; i++)
    queue->data[i] = (void*) SW_QUEUE_NULL;
}

void __sw_queue_free(queue_t* queue)
{
  munmap(queue, sizeof(queue_t));
}

uint8_t __sw_queue_empty(queue_t* queue)
{
  return ( (*queue->tail) == SW_QUEUE_NULL ) ? 1 : 0;
}
