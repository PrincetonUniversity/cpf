/* ********************************************
 * channel.h
 *
 * Create and manage channels and threads. Channel
 * is flow of data and control
 * ******************************************** */
#ifndef CHANNEL_H
#define CHANNEL_H

#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include "ppool_queue.h"

#define true 1
#define false 0

/* channel */
typedef struct channel_t *channel;

channel newChannel(unsigned processes, char **adj_mat, unsigned power);
void deleteChannel(channel chl);
unsigned getChannelProcesses(channel chl);
Queue** getChannelQueueMatrix(channel chl);

/* tid */
struct chl_tid_t {
    unsigned id;
    unsigned processes;
    channel chl;
    Queue *pQ;
    Queue *cQ;
    void * stack;
};

typedef struct chl_tid_t *chl_tid;
typedef void (*chl_runnable)(const int32_t arg);

chl_tid newTid(channel chl, unsigned id);
int deleteTid(chl_tid tid);
bool spawnProcess (chl_runnable fcn, chl_tid tid, int32_t arg, pid_t *pid_ptr);
static inline unsigned getId(chl_tid tid) { return tid->id; }

/* communication -- uninlined for correct linkage */
void chl_produce(const chl_tid tid, unsigned dest, const uint64_t value);
void chl_flushQueue(const chl_tid tid, unsigned dest);
uint64_t chl_consume(const chl_tid tid, unsigned orig);
void chl_produceChunk(const chl_tid tid, unsigned dest, void* addr, const size_t size);
void chl_consumeChunk(const chl_tid tid, unsigned orig, void* addr, const size_t size);

#endif
