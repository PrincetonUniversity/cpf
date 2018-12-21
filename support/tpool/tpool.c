#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <signal.h>


#define TPOOL_PRIVILEGE
#include "tpool.h"

#if TPOOL_AFFINITY > 0

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define TPOOL_DEBUG
#undef TPOOL_DEBUG

#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/syscall.h>


static pid_t gettid(void)
{
  return (pid_t) syscall(SYS_gettid);
}
#endif

// The main loop of a worker thread,
// tries to pull work from the work queue, and runs it
static void* tpool_worker_loop(void *tp) {
  Thread *thread = (Thread*)tp;
  ThreadPool tpool = thread->tpt_pool;

#if TPOOL_AFFINITY > 0
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  CPU_SET(thread->tpt_affinity, &affinity);
  sched_setaffinity(gettid(),sizeof(cpu_set_t), &affinity);
#endif

  for(;;) {

    // Wait until work is available
    while( 0 != sem_wait( & tpool->tpool_occupiedSlots ) ) {
      // we received an interrupt.
    }

    // Since we reached this point, we
    // know there is enough work for us
    // Lock the circular buffer to avoid
    // race
    Runnable work;
    Argument arg;
    ThreadHandle work_done_flag;
    pthread_mutex_lock( & tpool->tpool_lock );
    {

      // extract the work
      const struct tpool_work_t *slot = &tpool->tpool_workSlots[ tpool->tpool_workPull ];
      work = slot->work_routine;
      arg = slot->work_arg;
      work_done_flag = slot->work_done_flag;

      // advance the queue
      tpool->tpool_workPull = (tpool->tpool_workPull + 1) % tpool->tpool_numSlots;
    }
    // done modifying the circular buffer
    pthread_mutex_unlock( &tpool->tpool_lock );

    // tell producers there is room to submit more work
    sem_post( &tpool->tpool_freeSlots );

    // Do the work
    work(arg);

    // Possibly signal work_done_flag
    if( work_done_flag )
      sem_post( &work_done_flag->handle_semaphore );
  }

  pthread_exit(0);
  return 0;
}

ThreadPool tpool_new(int numWorkers) {
#ifdef TPOOL_DEBUG
  printf("tpool_new: Creating %d workers\n", numWorkers);
#endif
  ThreadPool tpool = (ThreadPool) malloc( sizeof(struct tpool_t) );
  tpool_init(tpool, numWorkers);
  return tpool;
}

void tpool_init(ThreadPool tpool, int numWorkers) {
  assert( numWorkers > 0 );

  // space to keep track of all threads
  tpool->tpool_numThreads = numWorkers;
  tpool->tpool_threads = (Thread *) malloc( numWorkers * sizeof(Thread) );

  // choose twice as many work slots as we have threads
  tpool->tpool_numSlots = 2 * numWorkers;
  tpool->tpool_workSlots = (struct tpool_work_t *) malloc( tpool->tpool_numSlots * sizeof(struct tpool_work_t) );

  // initialize the semaphores
  //  initially, no work available
  sem_init(&tpool->tpool_freeSlots, 1, tpool->tpool_numSlots);
  sem_init(&tpool->tpool_occupiedSlots, 1, 0);

  // the lock on the circular buffer
  pthread_mutex_init(&tpool->tpool_lock, 0);

  // initialize the circular queue
  tpool->tpool_workPut = tpool->tpool_workPull = 0;

  // create threads
  int i;
  for(i = 0; i < tpool->tpool_numThreads; ++i) {
    // each thread is a tpool_worker_loop
    // listening to this queue.


    Thread *context = & tpool->tpool_threads[i];
    context->tpt_pool = tpool;

#if TPOOL_AFFINITY > 0
    context->tpt_affinity = (i+1) % TPOOL_AFFINITY;
#endif

    pthread_create(&context->tpt_threadId, 0, tpool_worker_loop, context);
  }
}

// Destroy the thread pool
void tpool_finish(ThreadPool tpool) {

  // tell each thread to execute pthread_exit
  int i;
  for(i=0; i<tpool->tpool_numThreads; ++i)
    tpool_add_work(tpool, 0, pthread_exit, 0);

  // wait for workers to stop
  for(i=0; i<tpool->tpool_numThreads; ++i)
    pthread_join(tpool->tpool_threads[i].tpt_threadId, NULL);

  // free the mutex
  pthread_mutex_destroy( &tpool->tpool_lock );

  // free the semaphores
  sem_destroy( &tpool->tpool_occupiedSlots );
  sem_destroy( &tpool->tpool_freeSlots );

  // free pool structures
  free(tpool->tpool_workSlots);
  free(tpool->tpool_threads);
}

void tpool_delete(ThreadPool tpool) {
#ifdef TPOOL_DEBUG
  printf("tpool_delete: Destroying thread pool...\n");
#endif
  tpool_finish(tpool);
  free(tpool);
#ifdef TPOOL_DEBUG
  printf("tpool_delete: Destroyed\n");
#endif
}

ThreadHandle tpool_handle_new(void) {
  ThreadHandle th = (ThreadHandle) malloc( sizeof(struct tpool_thread_handle_t) );
  tpool_handle_init(th);
  return th;
}

void tpool_handle_init(ThreadHandle th) {
  sem_init( &th->handle_semaphore, 0, 0 );
}

void tpool_handle_finish(ThreadHandle th) {
  sem_destroy( &th->handle_semaphore );
}

void tpool_handle_delete(ThreadHandle th) {
  tpool_handle_finish(th);
  free(th);
}

// Put a work item onto the queue (producer)
void tpool_add_work(ThreadPool tpool, ThreadHandle handle, Runnable thread, Argument arg) {
#ifdef TPOOL_DEBUG
  printf("tpool_added_work: %x\n", handle);
#endif

  // Wait until there is room to submit work
  while( 0 != sem_wait( & tpool->tpool_freeSlots ) ) {
    // we were interrupted while waiting
  }

  // Since we reached this point, we
  // know there is room to add work.
  // Lock the circular buffer to avoid
  // race
  pthread_mutex_lock( & tpool->tpool_lock );
  {
    // inject the work
    struct tpool_work_t *slot = &tpool->tpool_workSlots[ tpool->tpool_workPut ];
    slot->work_routine = thread;
    slot->work_arg = arg;
    slot->work_done_flag = handle;

    // advance the queue
    tpool->tpool_workPut = (tpool->tpool_workPut + 1) % tpool->tpool_numSlots;
  }
  // done modifying the circular buffer
  pthread_mutex_unlock( &tpool->tpool_lock );

  // notify consumers that work is available
  sem_post( &tpool->tpool_occupiedSlots );

  return;
}

void tpool_wait(ThreadHandle th) {
#ifdef TPOOL_DEBUG
  printf("tpool_wait: %x\n", th);
#endif

  if( th )
    sem_wait( & th->handle_semaphore );

  return;
}

// Utility functions - especially useful
// in doall
ThreadHandle* tpool_new_handles(int numHandles)
{
#ifdef TPOOL_DEBUG
  printf("tpool_new_handles: %d\n", numHandles);
#endif
  int i;
  ThreadHandle* th = (ThreadHandle*)malloc(sizeof(ThreadHandle)*numHandles);
  for (i=0; i < numHandles; ++i)
    th[i] = tpool_handle_new();
  return th;
}

void tpool_wait_on_handles(ThreadHandle* th, int numHandles)
{
#ifdef TPOOL_DEBUG
  printf("tpool_wait_on_handles: %d\n", numHandles);
#endif
  int j;
  for (j=0; j < numHandles; ++j)
    tpool_wait(th[j]);
}

void tpool_delete_handles(ThreadHandle* th, int numHandles)
{
#ifdef TPOOL_DEBUG
  printf("tpool_delete_handles: %d\n", numHandles);
#endif
  int j;
  for (j=0; j < numHandles; ++j)
    tpool_handle_delete(th[j]);
  free(th);
}
