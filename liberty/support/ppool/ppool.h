#ifndef PPOOL_H
#define PPOOL_H

#include "ppool_channel.h"

typedef void (*Runnable)(void *);
typedef void * Argument;

// Initialize
void ppool_init(int num_processes, chl_runnable fcn);   // roughly initParallel()

// Finalize
void ppool_finish(void);                                // roughly finalizeParallel()

// Invoke parallel execution
void ppool_commence_parallel_exe(void);                 // roughly commenceParallelExecution()

// Wait until all the children complete
void ppool_wait(void);                                  // roughly tpool_wait()

// Get my tid
chl_tid ppool_get_my_tid(int id);

#endif

