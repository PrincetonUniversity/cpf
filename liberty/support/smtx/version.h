#ifndef VERSION_H
#define VERSION_H

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <setjmp.h>

#include "inline.h"
#include "sw_queue_astream.h"

/* Rules:
 *
 * 1. For every ver_begin there must be either a ver_end, a ver_misspec, or a
 * ver_terminate.
 *
 * 2. When misspeculation is detected, incoming queues must be flushed.
 *
 * 3. After all processes are in recovery mode, incoming queues must be flushed
 * again.
 * 
 * 4. No cycles among stages.
 * 
 * 5. No single iteration can completely fill a queue.
 *
 * 6. Only ver_begin, ver_check, ver_end, and ver_tryCommit check for
 * misspeculation.
 *
 * 7. The commit stage has the highest VER_TID.
 *
 * 8. VER_TIDs are signed.
 *
 * 9. Only ver_begin, ver_tryCommit, and ver_checkTryCommit read from queues.
 *
 * 10. Only ver_end, ver_read, and ver_writeX write to queues.
 *
 * 11. Messages from earlier processes are processed before later processes.
 *
 * 12. The commit stage processes versions in order, parallel stages might not.
 *
 * 13. Only the commit stage modifies the mode flag of the channel structure.
 *
 * 14. Producing is always safe.
 * 
 * 15. Consuming is only safe when it is known consuming will not block.
 *
 * 16. The try commit stage has the second highest VER_TID.
 */

typedef struct {
  int num_ps_threads;
} environment_t;

typedef enum {
  VER_OK = 0,
  VER_MISSPEC = -1,
  VER_RECOVER = -2,
  VER_TERM = -3,
  VER_CONT = -4
} ver_mode;

typedef struct {
  SW_Queue queues;
  uint32_t processes;

  bool * forceFlush;

  PAD(0, sizeof(SW_Queue) + sizeof(uint32_t) + sizeof(bool *));
  volatile ver_mode mode;
} *channel;

struct ver_tid_t;
typedef struct ver_tid_t *ver_tid;
typedef void (*ver_runnable)(const ver_tid, const uint64_t);

struct ver_tid_t {

  /* channel */
  channel chan; /* The channel this thread belongs to */
  
  int32_t prev; /* the VER_TID of the latest process to receive reads from. The
                   value is -1 for the first stage */
  uint32_t curr; /* my VER_TID */
  uint32_t next; /* the VER_TID of the earliest process to send writes to. The
                  * last stage sends writes to the commit stage only. */

  /* start-up */
  ver_runnable fcn;
  uint64_t arg; /* User extensible parameter. I'd make it (void *), but I want
                 * to have at least 64 bits. */

  /* cached */
  SW_Queue queue;
  SW_Queue tryCommitQueue;
  uint32_t processes;

  /* recovery */
  pid_t pid;
  void *stack;
  jmp_buf state;
};

/* Function listing */
int ver_getenv (environment_t * env);
channel ver_newChannel(unsigned processes);
channel ver_newChannel2(unsigned processes, char ** adj_mat);
void ver_deleteChannel(channel chan);

ver_mode ver_begin(const ver_tid id);
Inline ver_mode ver_end(const ver_tid id);

ver_mode ver_tryCommit(const ver_tid id);
ver_mode ver_checkTryCommit(const ver_tid id);

Weak void ver_misspec(const ver_tid id);
Weak void ver_terminate(const ver_tid id);

Inline ver_mode ver_check(ver_tid id);

Inline void ver_read(const ver_tid id,
                     const void *addr,
                     const uint64_t val,
                     const size_t size);

Weak void ver_writeTo(const ver_tid id,
                      const unsigned dest,
                      const void *addr,
                      const uint64_t val,
                      const size_t size);

Weak void ver_writeSilent(const ver_tid id, 
                          const void *addr,
                          const uint64_t val,
                          const size_t size);

Weak void ver_writeAll(const ver_tid id,
                       const void *addr,
                       const uint64_t val,
                       const size_t size);

void ver_empty(const ver_tid id);
void ver_flush(const ver_tid id);
void ver_flushAll(const ver_tid id);

void ver_setFlag(ver_tid tid, ver_mode mode);
void ver_waitNotMode(ver_tid tid, ver_mode mode);

void ver_signalRecovery(const ver_tid id);
void ver_waitAllRecover(const ver_tid id);
/* End function listing */

Inline SW_Queue ver_getQueue(const channel chan, unsigned x, unsigned y) {
  return chan->queues + chan->processes * x + y;
}

Inline ver_mode private_ver_broadcast(const ver_tid id, 
                                      const uint64_t token, 
                                      const uint64_t val) {
  const uint64_t processes = id->processes;
  SW_Queue currQueue = id->queue + id->next;
  const SW_Queue endQueue = id->queue + processes;
  
  for(; currQueue < endQueue; ++currQueue) {
    /* Send the token and the value */
    (sq_produce2)(currQueue, token, val, (sq_callback) ver_flushAll, id);
  }

  return id->chan->mode;
}

#define BOX64(X) ((uint64_t)(X))

#define private_ver_broadcast(ID,TOKEN,VAL)             \
  private_ver_broadcast((ID), BOX64(TOKEN), BOX64(VAL))

/*
 * End the current version.
 *
 * Returns the state of the channel.
 */
#include <stdio.h>
Inline ver_mode ver_end(const ver_tid id) {
  /* Inform the next stages that this is last message of the iteration, and that
     there was no misspeculation. */
  ver_mode state = private_ver_broadcast(id, NULL, VER_OK);
  if(*id->chan->forceFlush) {
    ver_flush(id);
    *id->chan->forceFlush = false;
  }
  return state;
}

/*
 * Flush and force all subsequent iterations to flush as well.
 */
Weak void ver_forceFlush(const ver_tid id) {
  *id->chan->forceFlush = true;
  ver_writeAll(id, id->chan->forceFlush, 
               BOX64(*id->chan->forceFlush), 
               sizeof(id->chan->forceFlush));
}

/*
 * Notify the commit stage of misspeculation.
 */
Weak void ver_misspec(const ver_tid id) {
  /* Inform the next stages that is last message of the iteration, and that
     there was misspeculation. */
  private_ver_broadcast(id, NULL, VER_MISSPEC);
}

/*
 * Notify the commit stage of termination.
 */
Weak void ver_terminate(const ver_tid id) {
  /* Inform the next stages that is last message of the iteration, and to
     terminate hereafter. */
  private_ver_broadcast(id, NULL, VER_TERM);
}

/*
 * Returns the state of the channel.
 */
Inline ver_mode ver_check(ver_tid id) {
  return id->chan->mode;
}

typedef enum {
  VER_WRITE = 0,
  VER_READ = 4
} ver_accType;

/*
 * Encodes an address, an access type, and the size of the access as a uint64_t.
 */
Inline uint64_t private_ver_encode(uint64_t addr, 
                                   ver_accType type, 
                                   uint64_t size) {
  return (addr << 3) | type | size;
}

/*
 * Maps [1, 2, 4, 8] => [0, 1, 2, 3]
 */
Inline uint64_t private_ver_encodeSize(size_t size) {

  /* The x86 assembly for this uses cmovb and is awesome. */
  if(size < 8) {
    return size >> 1;
  } else {
    return 3;
  }
}

#define private_ver_encode(ADDR,WRITE,SIZE)             \
  private_ver_encode(BOX64(ADDR), WRITE, BOX64(SIZE))

/*
 * Announce an access of a specified size to the commit process.
 */
Inline void private_ver_unicast(const ver_tid id,
                                const unsigned dest,
                                const void *addr,
                                const uint64_t val,
                                const size_t size,
                                const ver_accType msg) {

  uint64_t encodedSize = private_ver_encodeSize(size);

  uint64_t token = private_ver_encode(addr, msg, encodedSize);
  
  (sq_produce2)(id->queue + dest, token, val, (sq_callback) ver_flushAll, id);
}

Inline void ver_read(const ver_tid id,
                     const void *addr,
                     const uint64_t val,
                     const size_t size) {

  uint64_t encodedSize = private_ver_encodeSize(size);
  uint64_t token = private_ver_encode(addr, VER_READ, encodedSize);

  (sq_produce2)(id->tryCommitQueue, token, val, (sq_callback) ver_flushAll, id);
}

Weak void ver_writeTo(const ver_tid id,
                      const unsigned dest,
                      const void *addr,
                      const uint64_t val,
                      const size_t size) {
  private_ver_unicast(id, dest, addr, val, size, VER_WRITE);
}

Weak void ver_writeSilent(const ver_tid id, 
                          const void *addr,
                          const uint64_t val,
                          const size_t size) {
  ver_writeTo(id, id->processes - 2, addr, val, size);
  ver_writeTo(id, id->processes - 1, addr, val, size);
}

/*
 * Announce a write of specified size to all the following stages.
 */
Weak void ver_writeAll(const ver_tid id,
                       const void *addr,
                       const uint64_t val,
                       const size_t size) {
  
  uint64_t encodedSize = private_ver_encodeSize(size);

  uint64_t token = private_ver_encode(addr, VER_WRITE, encodedSize);
  private_ver_broadcast(id, token, val);
}

#define ver_read(ID,ADDR,VAL)                   \
  ver_read(ID,                                  \
           ADDR,                                \
           BOX64(VAL),                          \
           sizeof(*(ADDR)))

#define ver_writeTo(ID,DEST,ADDR,VAL)                           \
  ver_writeTo(ID, DEST, ADDR, BOX64(VAL), sizeof(*(ADDR)))

#define ver_writeSilent(ID,ADDR,VAL)                            \
  ver_writeSilent(ID, ADDR, BOX64(VAL), sizeof(*(ADDR)))

#define ver_writeAll(ID,ADDR,VAL)                       \
  ver_writeAll(ID, ADDR, BOX64(VAL), sizeof(*(ADDR)))

#undef private_ver_encode
#undef private_ver_broadcast

#pragma GCC poison private_ver_encode
#pragma GCC poison private_ver_broadcast
#pragma GCC poison private_ver_unicast

#endif /* VERSION_H */
