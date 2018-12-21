/** ***********************************************/
/** *** SW Queue with Supporting Variable Size ****/
/** ***********************************************/
#ifndef SHM_QUEUE_H
#define SHM_QUEUE_H

#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>   /* usleep */
#include <string.h>   /* memcpy */
#include <stdlib.h>   /* for exit */
#include <stdio.h>

#define CACHELINE_SIZE 128
#define CACHELINE_UNIT (128/8)

#define PAD(suffix, size) char padding ## suffix [CACHELINE_SIZE - size]

typedef struct {
    uint64_t p_inx;
    uint64_t p_size;
    uint64_t p_mask;
    uint64_t p_margin;
    PAD(1, sizeof(uint64_t)*4);

    uint64_t p_glb_inx;
    volatile uint64_t *ptr_c_glb_inx;
    PAD(2, sizeof(uint64_t) + sizeof(volatile uint64_t *));

    uint64_t c_inx;
    uint64_t c_size;
    uint64_t c_mask;
    uint64_t c_margin;
    PAD(3, sizeof(uint64_t)*4);

    uint64_t c_glb_inx;
    volatile uint64_t *ptr_p_glb_inx;
    PAD(4, sizeof(uint64_t) + sizeof(volatile uint64_t *));

    uint64_t *data;
    PAD(5, sizeof(uint64_t));
} shm_queue_t, *SHM_Queue;

// *****************************************
// ******* Inside-Used Functions ***********
// *****************************************
static inline uint64_t shm_modSub(uint64_t minuend, uint64_t subtrahend, uint64_t mask) {
    return (minuend - subtrahend) & mask;
}

static inline void shm_waitConsumer(SHM_Queue queue, uint64_t distance)
{
    uint64_t size;
    while((size=shm_modSub(queue->p_inx, *queue->ptr_c_glb_inx, queue->p_mask)) > distance) usleep(1);
    queue->p_margin =queue->p_size - size - 1;
}

static inline size_t shm_waitAllocated(SHM_Queue queue)
{
    *queue->ptr_c_glb_inx = queue->c_inx;
    while(*queue->ptr_p_glb_inx == queue->c_inx) usleep(1);
    return shm_modSub(*queue->ptr_p_glb_inx, queue->c_inx, queue->c_mask);
}

// ******************************************
// ********** Public Functions **************
// ******************************************
SHM_Queue shm_createQueue(unsigned size_power);
void shm_freeQueue(SHM_Queue q);
void shm_emptyQueue(SHM_Queue queue);

static inline void shm_flushQueue(SHM_Queue queue)
{
    *queue->ptr_p_glb_inx = queue->p_inx;
}

static inline void shm_produce(SHM_Queue queue, uint64_t value)
{
    if(!(queue->p_margin)){
        shm_flushQueue(queue);
        shm_waitConsumer(queue, 3*queue->p_size/4);
    }
    queue->data[queue->p_inx] = value;
    queue->p_inx++;
    queue->p_inx &= queue->p_mask;
    queue->p_margin--;
}

static inline uint64_t shm_consume(SHM_Queue queue)
{
    if(!queue->c_margin){
        queue->c_margin = shm_waitAllocated(queue);
    }
    uint64_t val = queue->data[queue->c_inx];
    queue->c_inx++;
    queue->c_inx &= queue->c_mask;
    queue->c_margin--;
    return val;
}

static inline void shm_produceChunk(SHM_Queue queue, void *addr, size_t size)
{
    size_t space;
    char* caddr = (char *) addr;
    size_t inx = size/sizeof(uint64_t);
    if(size%sizeof(uint64_t)>0) inx++;

    if(queue->p_margin < inx){
        shm_flushQueue(queue);
        shm_waitConsumer(queue, (uint64_t) inx);
    }

    space = queue->p_size - queue->p_inx;
    if(size > space*sizeof(uint64_t)){
        memcpy(&queue->data[queue->p_inx], caddr, space*sizeof(uint64_t));
        inx -= space;
        size -= space*sizeof(uint64_t);
        caddr += space*sizeof(uint64_t);
        queue->p_inx+=space;
        queue->p_inx &= queue->p_mask;
        queue->p_margin-=space;
    }
    memcpy(&queue->data[queue->p_inx], caddr, size);
    queue->p_inx+=inx;
    queue->p_inx &= queue->p_mask;
    queue->p_margin-=inx;

    shm_flushQueue(queue);
}

static inline void shm_consumeChunk(SHM_Queue queue, void *addr, size_t size)
{
    size_t space;
    char* caddr = (char *) addr;
    size_t inx = size/sizeof(uint64_t);
    if(size%sizeof(uint64_t)>0) inx++;

    while(queue->c_margin < inx){
        queue->c_margin = shm_waitAllocated(queue);
    }

    space = queue->c_size - queue->c_inx;
    if(size > space*sizeof(uint64_t)){
        memcpy(caddr, &queue->data[queue->c_inx], space*sizeof(uint64_t));
        inx -= space;
        size -= space*sizeof(uint64_t);
        caddr += space*sizeof(uint64_t);
        queue->c_inx+=space;
        queue->c_inx &= queue->c_mask;
        queue->c_margin-=space;
    }
    memcpy(caddr, &queue->data[queue->c_inx], size);
    queue->c_inx+=inx;
    queue->c_inx &= queue->c_mask;
    queue->c_margin-=inx;
}
#endif

