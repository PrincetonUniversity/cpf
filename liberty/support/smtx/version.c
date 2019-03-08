#include <stdlib.h>
#include <stdio.h>
#include "version.h"

/*
 * Parse environment variables.
 */
int ver_getenv (environment_t * env) {
  char * env_num_ps_threads;
  env_num_ps_threads = getenv ("NUM_PS_THREADS");
  if (env_num_ps_threads == NULL) {
    fprintf (stderr, "NUM_PS_THREADS environment variable not set. Rerun after setting it.\n");
    exit (EXIT_FAILURE);
  }
  env->num_ps_threads = atoi (env_num_ps_threads);
  return 0; /*Success*/
}

/*
 * Create a new channel of versioned memory.
 *
 * processes - the number of processes in this channel
 */
channel ver_newChannel(unsigned processes) {

  channel chan = (channel) mmap((void *) (1UL << 32),
                                sizeof(*(channel) NULL), 
                                PROT_WRITE | PROT_READ, 
                                MAP_SHARED | MAP_ANONYMOUS, 
                                -1, 
                                (off_t) 0);

  chan->processes = processes;

  chan->queues = sq_createQueueBlock(processes * processes);
  for(unsigned i = 0; i < processes; ++i) {
    
    for(unsigned j = i + 1; j < processes; ++j) {
      sq_initQueue(ver_getQueue(chan, i, j));
    }
  }
  
  chan->forceFlush = (bool *) malloc(sizeof(chan->forceFlush));
  *chan->forceFlush = false;

  chan->mode = VER_OK;

  return chan;
}

/*
 * Create a new channel of versioned memory allocating queues as
 * specified in the adjacency matrix.
 *
 * processes - the number of processes in this channel
 * adj_mat   - adjacency matrix that specifies which processes
 *             communicate with one another
 */
channel ver_newChannel2(unsigned processes, char ** adj_mat) {

  channel chan = (channel) mmap((void *) (1UL << 32),
                                sizeof(*(channel) NULL), 
                                PROT_WRITE | PROT_READ, 
                                MAP_SHARED | MAP_ANONYMOUS, 
                                -1, 
                                (off_t) 0);

  chan->processes = processes;

  chan->queues = sq_createQueueBlock(processes * processes);
  for(unsigned i = 0; i < processes; ++i) {
    
    for(unsigned j = 0; j < processes; ++j) {
      if (adj_mat[i][j]) {
        sq_initQueue(ver_getQueue(chan, i, j));
      }
    }
  }

  chan->forceFlush = (bool *) malloc(sizeof(chan->forceFlush));
  *chan->forceFlush = false;
  
  chan->mode = VER_OK;

  return chan;
}
/*
 * Delete a versioned memory channel
 */
void ver_deleteChannel(channel chan) {
  sq_freeQueueBlock(chan->queues, chan->processes * chan->processes);
  free(chan->forceFlush);
  munmap(chan, sizeof(*(channel) NULL));
}

Inline bool ver_shouldConsume(const ver_tid id, SW_Queue queue) {

  if(sq_canConsume(queue)) {
    return true;
  }
  
  ver_flushAll(id);

  for(;;) {
    if(sq_canConsume(queue)) {
      return true;
    } else if(id->chan->mode != VER_OK) {
      return false;
    }
    usleep(10);
  }
}

#define COMMIT(TYPE,IS_READ,ADDR,VAL)           \
  do {                                          \
    TYPE *p = (TYPE *) ADDR;                    \
    if(IS_READ) {                               \
      if(*p != (TYPE) VAL) {                    \
        return VER_MISSPEC;                     \
      }                                         \
    } else {                                    \
      *p = (TYPE) VAL;                          \
    }                                           \
  } while(false)

/*
 * Returns false if this was the last token for a particular iteration. True
 * otherwise.
 */
Inline ver_mode ver_applyUpdates(const ver_tid id, 
                                 const SW_Queue queue, 
                                 int processReads) {

  uint64_t token = (sq_consume)(queue, (sq_callback) ver_flushAll, id);
  uint64_t value = (sq_consume)(queue, (sq_callback) ver_flushAll, id);

  if(token) {

    /* Decode the message */
    uint64_t addr = (uint64_t) (((int64_t)token) >> 3);
    bool isRead = (token & VER_READ) && processReads;
    uint64_t size = token & 3;

    /* Update memory with writes from the previous stage */
    if     (size == 0) COMMIT(uint8_t,  isRead, addr, value);
    else if(size == 1) COMMIT(uint16_t, isRead, addr, value);
    else if(size == 2) COMMIT(uint32_t, isRead, addr, value);
    else               COMMIT(uint64_t, isRead, addr, value);

    return VER_CONT;

  } else {
    return (ver_mode) value;
  }
}

/*
 * Begin a new version.
 *
 * Returns the state of the channel
 */
ver_mode ver_begin(const ver_tid id) {

  for(int from = 0; from <= id->prev; ++from) {

    /* Get the appropriate queue */
    SW_Queue currQueue = ver_getQueue(id->chan, (unsigned) from, id->curr);

    /* Consume the next update from the queue */
    ver_mode state;
    do {

      if(!ver_shouldConsume(id, currQueue))
        return id->chan->mode;

      state = ver_applyUpdates(id, currQueue, false);
      
    } while(state == VER_CONT);
    
    if(state != VER_OK) {
      return state;
    }
  }

  return id->chan->mode;
}

/*
 * Try to commit.
 */
ver_mode ver_tryCommit(const ver_tid id) {
  
  for(int from = 0; from <= id->prev; ++from) {

    /* Get the appropriate queue */
    SW_Queue currQueue = ver_getQueue(id->chan, (unsigned) from, id->curr);

    /* Consume the next update from the queue */
    ver_mode state;
    do {

      if(!ver_shouldConsume(id, currQueue))
        return id->chan->mode;

      state = ver_applyUpdates(id, currQueue, true);
      
    } while(state == VER_CONT);
    
    if(state != VER_OK) {

      if(state == VER_MISSPEC) {
        ver_misspec(id);

      } else if(state == VER_TERM) {
        ver_terminate(id);

      } else {
        assert(false && "Unkown mode");
      }
        
      return state;
    }
  }
  
  return id->chan->mode;
}

/*
 * Check the try commit stage
 */
ver_mode ver_checkTryCommit(const ver_tid id) {
  
  uint32_t commitProc = id->processes - 1;
  uint32_t tryCommitProc = id->processes - 2;

  assert(id->curr == commitProc && 
         "Only the commit stage can check the try commit stage");

  SW_Queue currQueue = ver_getQueue(id->chan, tryCommitProc, commitProc);
  
  /* No need for a double wait since this is the commitProc */
  uint64_t token = (sq_consume)(currQueue, (sq_callback) ver_flushAll, id);
  uint64_t value = (sq_consume)(currQueue, (sq_callback) ver_flushAll, id);
  
  assert(!token && "The try commit stage may not do versioned reads or writes");
  return (ver_mode) value;
}

/*
 * Empty my incoming queues.
 */
void ver_empty(const ver_tid id) {

  /* For each prior stage... */
  for(int from = 0; from <= id->prev; ++from) {
    /* Get the appropriate queue and empty it */
    SW_Queue currQueue = ver_getQueue(id->chan, (unsigned) from, id->curr);
    sq_emptyQueue(currQueue);
  }
}

/*
 * Reverse flush my incoming queues.
 */
static void ver_reverseFlush(const ver_tid id) {

  /* For each prior stages... */
  for(int from = 0; from <= id->prev; ++from) {

    /* Get the appropriate queue and empty it */
    SW_Queue currQueue = ver_getQueue(id->chan, (unsigned) from, id->curr);
    sq_reverseFlush(currQueue);
  }

  unsigned processes = id->processes;
  if(id->curr ==  processes - 1) {
    SW_Queue currQueue = ver_getQueue(id->chan, processes - 2, id->curr);
    sq_reverseFlush(currQueue);
  }
}

/*
 * Flush my buffered outgoing queues.
 */
void ver_flush(const ver_tid id) {

  /* For each prior stages... */
  for(unsigned to = id->next; to < id->chan->processes; ++to) {

    /* Get the appropriate queue */
    SW_Queue currQueue = id->queue + to;
    sq_flushQueue(currQueue);
  }
}

void ver_flushAll(const ver_tid id) {
  ver_flush(id);
  ver_reverseFlush(id);
}

/*
 * Set the flag.
 */
void ver_setFlag(ver_tid id, ver_mode mode) {
  id->chan->mode = mode;
}

/*
 * Wait for not a mode.
 */
void ver_waitNotMode(ver_tid id, ver_mode mode) {
  while(id->chan->mode == mode) usleep(10);
}

/*
 * Announce that you have entered recovery mode.
 */
void ver_signalRecovery(const ver_tid id) {
  
  SW_Queue commitQueue = id->queue + id->processes - 1;

  (sq_produce2)(commitQueue, BOX64(NULL), BOX64(VER_RECOVER), 
                (sq_callback) ver_flushAll, id);
  sq_flushQueue(commitQueue);
}

/*
 * Wait for all preceding stage to send the recovery token.
 */
void ver_waitAllRecover(const ver_tid id) {

  uint32_t commitProc = id->processes - 1;

  assert(id->curr == commitProc && 
         "Only the commit stage can check for recovery");

  /* For each prior stages... */
  for(uint32_t from = 0; from < commitProc; ++from) {

    /* Get the appropriate queue */
    SW_Queue currQueue = ver_getQueue(id->chan, from, id->curr);

    /* Consume the next token from the queue */
    uint64_t token = 
      (sq_consume)(currQueue, (sq_callback) ver_flushAll, id);
    
    uint64_t value = 
      (sq_consume)(currQueue, (sq_callback) ver_flushAll, id);

    assert(token == 0 && "Not a control token");
    assert(value == (uint64_t) VER_RECOVER && "Not the recovery token");
  }
}

#ifdef UNIT_TEST

int main(int argc, char **argv) {
  
  (void) argc;
  (void) argv;

  channel chan = ver_newChannel(4);
  ver_deleteChannel(chan);
  return 0;
}

#endif /* UNIT_TEST */
