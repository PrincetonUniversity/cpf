#ifndef LLVM_LIBERTY_SPEC_PRIV_SMTX_SW_QUEUE_H
#define LLVM_LIBERTY_SPEC_PRIV_SMTX_SW_QUEUE_H

#include <stdint.h>

#define PADDING 64
#define PAD(suffix, size) char padding ## suffix [PADDING - size]

#define QUEUE_SIZE 8192*2 /* // 65536 // 128 // 16384 // 8192*/

typedef struct {
    void * volatile *head;
    void * volatile *prealloc;
    PAD(1, sizeof(void *)*2);
    void * volatile *tail;
    void * volatile *prealloc_tail;
    int volatile finished[1];
    PAD(2, sizeof(void *)*2+sizeof(int*));
    void *data[QUEUE_SIZE];
} queue_t;

queue_t* __sw_queue_create(void);
void     __sw_queue_produce(queue_t* queue, void* value);
void*    __sw_queue_consume(queue_t* queue);
void     __sw_queue_flush(queue_t* queue);
void     __sw_queue_clear(queue_t* queue);
void     __sw_queue_reset(queue_t* queue);
void     __sw_queue_free(queue_t*);
uint8_t  __sw_queue_empty(queue_t*);

#undef PAD
#undef PADDING

#endif
