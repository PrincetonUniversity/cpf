#ifndef TPOOL_H
#define TPOOL_H

#include <semaphore.h>

/* Affinity scheduling.
 * If 0, don't set processor affinity for this thread.
 * If >0, then set affinity in a round-robin fashion
 * along processors 0 ... TPOOL_AFFINITY-1
 */
#define TPOOL_AFFINITY      (24)

typedef void (*Runnable)(void *);
typedef void * Argument;

struct tpool_thread_handle_t {
  sem_t               handle_semaphore;
};
typedef struct tpool_thread_handle_t *ThreadHandle;

// The thread work structure
struct tpool_work_t {
	Runnable            work_routine;
  Argument            work_arg;
  ThreadHandle        work_done_flag;
};

struct tpool_thread_t {
  pthread_t           tpt_threadId;
#if TPOOL_AFFINITY > 0
  int                 tpt_affinity;
#endif

  struct tpool_t *    tpt_pool;
};
typedef struct tpool_thread_t Thread;

// The thread pool structure
struct tpool_t {
  // set of running threads
	int               tpool_numThreads;
	Thread *          tpool_threads;

  // keep track of how many work
  // slots are free or occupied
  sem_t             tpool_freeSlots;
  sem_t             tpool_occupiedSlots;

  // This lock guards the circular buffer
  pthread_mutex_t   tpool_lock;

  // circular buffer work queue
  int               tpool_numSlots;
	struct tpool_work_t *    tpool_workSlots;

  // keep track of where the next
  // work unit will be put or pulled
  int               tpool_workPut;
  int               tpool_workPull;

};
typedef struct tpool_t * ThreadPool;


#ifndef TPOOL_PRIVILEGE
  // make all the fields of these structures private
#pragma GCC poison handle_semaphore
#pragma GCC poison work_routine
#pragma GCC poison work_arg
#pragma GCC poison work_done_flag
#pragma GCC poison tpool_numThreads
#pragma GCC poison tpool_threads
#pragma GCC poison tpool_freeSlots
#pragma GCC poison tpool_occupiedSlots
#pragma GCC poison tpool_lock
#pragma GCC poison tpool_numSlots
#pragma GCC poison tpool_workSlots
#pragma GCC poison tpool_workPut
#pragma GCC poison tpool_workPull
#endif

// Create a new thread pool
ThreadPool tpool_new(int num_worker_threads);             // allocate, then initialize
void tpool_init(ThreadPool tp, int num_worker_threads);   // initialize already allocated

// stop a thread pool, free its memory
void tpool_finish(ThreadPool tp);                         // finalize
void tpool_delete(ThreadPool tp);                         // finalize then free mem

// Initialize, destroy a thread handle
ThreadHandle tpool_handle_new(void);                      // allocate then initialize
void tpool_handle_init(ThreadHandle th);                  // initialize already allocated

void tpool_handle_finish(ThreadHandle th);                // finalize
void tpool_handle_delete(ThreadHandle th);                // finalize then free mem

// add a work item to the pool
void tpool_add_work(ThreadPool tp, ThreadHandle th, Runnable thread, Argument arg);

// wait for a particular work item to complete
void tpool_wait(ThreadHandle th);

// Utility functions - especially useful
// in doall
ThreadHandle* tpool_new_handles(int numHandles);
void tpool_wait_on_handles(ThreadHandle* th, int numHandles);
void tpool_delete_handles(ThreadHandle* th, int numHandles);
#endif

