#include <stdio.h>      /* for perror */
#include <errno.h>      /* for perror */
#include <malloc.h>     /* for malloc */

#include "ppool_channel.h"  /* header */
#include "ppool_log.h"      /* for LOG */

struct channel_t{
    unsigned processes;
    Queue **q;

};

channel newChannel(unsigned processes, char **adj_mat, unsigned power) {
    unsigned i, j;
    channel chl;

    chl = (channel) malloc(sizeof(struct channel_t));
    if(chl == NULL) {
        perror("newChannel: malloc for channel");
        return NULL;
    }

    chl->processes = processes;
    chl->q = (Queue **) malloc(sizeof(Queue *) * processes);
    if(chl->q == NULL) {
        perror("newChannel: malloc for q matrix");
        return NULL;
    }
    for(i=0; i<processes; i++) {
        chl->q[i] = (Queue *) malloc(sizeof(Queue) * processes);
        if(chl->q[i] == NULL) {
            perror("newChannel: malloc for q matrix");
            return NULL;
        }
        for(j=0; j<processes; j++) {
            if(adj_mat[i][j]) chl->q[i][j] = createQueue(power);
        }
    }

    return chl;
}

void deleteChannel(channel chl) {
    int i, j;
    int processes;
    if(chl==NULL) return;
    processes = chl->processes;
    for(i=0; i<processes; i++) {
        for(j=0; j<processes; j++) {
            if(chl->q[i][j]) {
                freeQueue(chl->q[i][j]);
            }
        }
        free(chl->q[i]);
    }
    free(chl->q);
    free(chl);
}

unsigned getChannelProcesses(channel chl) {
    return chl->processes;
}

Queue** getChannelQueueMatrix(channel chl) {
    return chl->q;
}

void chl_produce(const chl_tid tid, unsigned dest, const uint64_t value){
    assert(tid!=NULL);
    assert(dest < tid->processes);
    assert(tid->pQ[dest] != NULL);
    produce(tid->pQ[dest], value);
}

void chl_flushQueue(const chl_tid tid, unsigned dest){
    assert(tid!=NULL);
    assert(dest < tid->processes);
    assert(tid->pQ[dest] != NULL);
    flushQueue(tid->pQ[dest]);
}

uint64_t chl_consume(const chl_tid tid, unsigned orig){
    assert(tid!=NULL);
    assert(orig < tid->processes);
    assert(tid->cQ[orig] != NULL);
    return consume(tid->cQ[orig]);
}

void chl_produceChunk(const chl_tid tid, unsigned dest, void* addr, const size_t size){
    assert(tid!=NULL);
    assert(dest < tid->processes);
    assert(tid->pQ[dest] != NULL);
    produceChunk(tid->pQ[dest], addr, size);
}

void chl_consumeChunk(const chl_tid tid, unsigned orig, void* addr, const size_t size){
    assert(tid!=NULL);
    assert(orig < tid->processes);
    assert(tid->cQ[orig] != NULL);
    consumeChunk(tid->cQ[orig], addr, size);
}
