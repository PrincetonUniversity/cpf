#include <stdint.h>
#include <sys/mman.h>  /* for mmap */
#include <stdio.h>
#include <stdlib.h>    /* for exit */
#include <assert.h>

#include "ppool_shm_queue.h"

// *****************************************
// ******* Detail Implementation ***********
// *****************************************

SHM_Queue shm_createQueue(unsigned size_power)
{
    unsigned size = 1 << size_power;
    shm_queue_t *queue = (shm_queue_t *)mmap(0, sizeof(shm_queue_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(queue == (shm_queue_t *) -1) {
        perror("shm_createQueue");
        exit(1);
    }

    queue->data = (uint64_t *)mmap(0, sizeof(uint64_t)*size, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(queue->data == (void *) -1) {
        perror("shm_createQueue");
        exit(1);
    }

    queue->p_size = size;
    queue->c_size = size;
    queue->p_mask = size - 1;
    queue->c_mask = size - 1;
    queue->p_inx = 0;
    queue->c_inx = 0;
    queue->p_glb_inx = 0;
    queue->c_glb_inx = 0;
    queue->c_margin = 0;
    queue->p_margin = size-CACHELINE_UNIT;
    queue->ptr_c_glb_inx = &(queue->c_glb_inx);
    queue->ptr_p_glb_inx = &(queue->p_glb_inx);

    return queue;
}

void shm_freeQueue(SHM_Queue q)
{
    shm_queue_t *queue = (shm_queue_t *) q;
    munmap(queue->data, (size_t) (sizeof(uint64_t)*queue->p_size));
    munmap(queue, sizeof(shm_queue_t));
}

// flush queue: Should be called by CONSUMER!!
// It does not flush data which a producer are producing.
// So, need to guarantee the producer does not produce anything.
void shm_emptyQueue(SHM_Queue queue){
    queue->c_inx = *queue->ptr_p_glb_inx;
    queue->c_margin = 0;
}
