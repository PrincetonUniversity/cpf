#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <xmmintrin.h>


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

#include "constants.h"
#include "config.h"

#include "heap.h"
#include "io.h"
#include "timer.h"
#include "pcb.h"
#include "private.h"
#include "fiveheaps.h"
#include "checkpoint.h"
#include "api.h"

#if (AFFINITY & OS2PHYS) != 0
// Map of linux cpu number to physical core number
static const int oscpu2physcore[NUM_PROCS] =
/*
  // P0  P1  P2  P3
  {   0,  1,  2,  3,
      4,  9, 14, 19,
      5, 10, 15, 20,
      6, 11, 16, 21,
      7, 12, 17, 22,
      8, 13, 18, 23
  };
*/
  {
  /* P0: */ 0,  4,  5,  6,  7,  8,
  /* P1: */ 1,  9, 10, 11, 12, 13,
  /* P2: */ 2, 14, 15, 16, 17, 18,
  /* P3: */ 3, 19, 20, 21, 22, 23
  };
#define CORE(n)     ( oscpu2physcore[n] )
#else
#define CORE(n)     ( n )
#endif

// Worker management
static Wid numWorkers;
static Wid myWorkerId;

static Bool runOnEveryIter;

// parallel stage replica id
static Wid pstage_replica_id = NOT_A_PARALLEL_STAGE;

// The global iteration number; maintained by
// each worker.  Incremented by __specpriv_end_iter()
static Iteration currentIter = 0;

// Old CPU affinity
static cpu_set_t old_affinity;

#if JOIN == WAITPID
static pid_t workerPids[ MAX_WORKERS ];
#endif

#if JOIN == SPIN
struct sigaction old_sigchld;
#endif

#if SIMULATE_MISSPEC != 0
static Iteration simulateMisspeculationAtIter;
#endif

// ---------------------------------------------------------
// Scope management: program, parallel region, worker,
// and iteration...

// Called once on program startup by main process
void __parallel_begin(void)
{
  // We are in the main process.
  myWorkerId = MAIN_PROCESS;

  // Determine number of workers from environment
  // variable.
  numWorkers = 2;
  const char *nw = getenv("NUM_WORKERS");
  if( nw )
  {
    int n = atoi(nw);
    assert( 1 <= n && n <= MAX_WORKERS );
    numWorkers = (Wid) n;
  }

  DEBUG(printf("Available workers: %u\n", numWorkers));

  // Save old affinity
  sched_getaffinity(0, sizeof(cpu_set_t), &old_affinity);

  // Set affinitiy: only processor zero
#if (AFFINITY & MP0STARTUP) != 0
  cpu_set_t affinity;
  CPU_ZERO( &affinity );
  CPU_SET( CORE(0), &affinity );
  sched_setaffinity(0, sizeof(cpu_set_t), &affinity );
#endif
}

// Called once on program startup by main process
// for separation speculation
void __specpriv_begin(void)
{
  // We are in the main process.
  myWorkerId = MAIN_PROCESS;

#if SIMULATE_MISSPEC != 0
  simulateMisspeculationAtIter = ~0U;
  const char *smai = getenv("SIMULATE_MISSPEC_ITER");
  if( smai )
  {
    int n = atoi(smai);
    assert( 0 <= n );
    simulateMisspeculationAtIter = (Iteration)n;
  }
#endif

  __specpriv_initialize_main_heaps();
  __specpriv_init_private();

#if JOIN == SPIN
  // Replace the SIGCHLD handler with SIG_IGN.
  // According to POSIX.1-2001, setting this handler
  // will tell the kernel that child processes will
  // NOT become zombies when they die.
  struct sigaction new_sigchld;
  new_sigchld.sa_handler = SIG_IGN;
  sigemptyset( &new_sigchld.sa_mask );
  new_sigchld.sa_flags = SA_NOCLDWAIT;
  sigaction(SIGCHLD, &new_sigchld, &old_sigchld);
#endif

}

// Called once by main process on program shutdown
void __parallel_end(void)
{
  assert( myWorkerId == MAIN_PROCESS );

#if (AFFINITY & MP0STARTUP) != 0
  // Reset affinity
  sched_setaffinity(0, sizeof(cpu_set_t), &old_affinity);
#endif
}

// Called once by main process on program shutdown
// for separation speculation
void __specpriv_end(void)
{
  assert( myWorkerId == MAIN_PROCESS );

  __specpriv_destroy_main_heaps();
}

// Called when misspeculation is detected.
// Triggers the recovery process.
void __specpriv_misspec(const char *reason)
{
  __specpriv_misspec_at(currentIter, reason);
}

void __specpriv_misspec_at(Iteration iter, const char *reason)
{
  ParallelControlBlock *pcb = __specpriv_get_pcb();

  pcb->misspeculated_worker = myWorkerId;
  pcb->misspeculated_iteration = iter;
  pcb->misspeculation_reason = reason;
  pcb->misspeculation_happened = 1;

#if JOIN == SPIN
  pcb->workerDoneFlags[ myWorkerId ] = 1;
#endif

  __specpriv_destroy_worker_heaps();

#if DEBUG_MISSPEC || DEBUGGING
  fprintf(stderr,"Misspeculation detected at iteration %d by worker %d\n", iter, myWorkerId);
  if( reason )
    fprintf(stderr,"Reason: %s\n", reason);
#endif

  _exit(0);
}

static void __specpriv_sig_helper(int sig, siginfo_t *siginfo, void *dummy)
{
  __specpriv_misspec("Segfault");
}

// Called by __specpriv_spawn_workers on each worker
// after they are spawned
static void __specpriv_worker_starts(Iteration firstIter, Wid wid)
{
  TIME(worker_begin_invocation);

  // First, a policy to make the spawn work better...
#if (AFFINITY & SLEEP0) != 0
  // 'sleep0'
  sleep(0);
#endif

  assert(wid != MAIN_PROCESS );
  assert(wid < numWorkers);
  myWorkerId = wid;

  // true by default
  runOnEveryIter = 1;

#if (AFFINITY & RRPUNT) != 0
  // 'rrpunt'
  // First, set affinity to a single processor != 0,
  // selected uniformly according to wid.
  // This will punt this worker OFF of the
  // main-process' processor.
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  CPU_SET( CORE( 1 + (myWorkerId % (NUM_PROCS-1) ) ), &affinity );
  sched_setaffinity( 0, sizeof(cpu_set_t), &affinity );
#endif
#if (AFFINITY & RRPUNT0) != 0
  // 'rrpunt0'
  // First, set affinity to a single processor
  // selected uniformly according to wid.
  // This will punt this worker OFF of the
  // main-process' processor.
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  CPU_SET( CORE( (myWorkerId+1) % NUM_PROCS ), &affinity );
  sched_setaffinity( 0, sizeof(cpu_set_t), &affinity );
#endif

#if (AFFINITY & ALLBUT1) != 0
  // 'allbut1'
  // Next, relax the affinity so it can run on many processors.
  // Since this set is a superset of the previous, it does
  // not force a migration.
  // The first N can run on any except proc 0.
  // The later ones can run on any processor.
  memcpy( &affinity, &old_affinity, sizeof(cpu_set_t) );
  if( myWorkerId + 1 < NUM_PROCS )
    CPU_CLR( CORE(0), &affinity );
  sched_setaffinity( 0, sizeof(cpu_set_t), &affinity );
#endif
#if (AFFINITY & ANY) != 0
  //'any'
  // relax affinity to ANY processor
  sched_setaffinity( 0, sizeof(cpu_set_t), &old_affinity);
#endif
#if (AFFINITY & FIX) != 0
  // 'fix'
  // Do not relax affinity; the worker is bound to a single proc.
#endif

  __specpriv_initialize_worker_heaps();

  __specpriv_set_first_iter(firstIter);

  // Initialize structure for deferred IO.
  __specpriv_reset_worker_io();

  // capture all signals -> misspeculate
  struct sigaction replacement;
  replacement.sa_flags = SA_SIGINFO;
  sigemptyset( &replacement.sa_mask );
  replacement.sa_sigaction = &__specpriv_sig_helper;
  sigaction( SIGSEGV, &replacement, 0 );

  TIME(worker_enter_loop);
}

// Called by a worker when it is done working.
void __specpriv_worker_finishes(Exit exitTaken)
{
  assert( myWorkerId != MAIN_PROCESS );

  DEBUG(printf("Worker %u finishing with exitTaken:%u.\n", myWorkerId,
               exitTaken));

  TIME(worker_exit_loop);

  __specpriv_worker_perform_checkpoint(1);
  __specpriv_destroy_worker_heaps();

  __specpriv_get_pcb()->exit_taken = exitTaken;

#if JOIN == SPIN
  // Tell main process we have completed
  // (they can read this long before waitpid()
  // would finish)
  ParallelControlBlock *pcb = __specpriv_get_pcb();
  pcb->workerDoneFlags[ myWorkerId ] = 1;
#endif

  TIME(worker_end_invocation);
  TOUT( __specpriv_print_worker_times() );

  DEBUG(printf("Worker %u finished.\n", myWorkerId));
  _exit(0);
}

Iteration __specpriv_current_iter(void)
{
  if (runOnEveryIter)
    return currentIter;

  // Return global iteration count instead of thread-specific one
  if ( myWorkerId == MAIN_PROCESS)
    return (currentIter * numWorkers);

  Iteration globalCurIter = myWorkerId + (currentIter * numWorkers);
  return globalCurIter;
}

Bool __specpriv_runOnEveryIter()
{
  return runOnEveryIter;
}

Wid __specpriv_my_worker_id(void)
{
  return myWorkerId;
}

Bool __specpriv_i_am_main_process(void)
{
  return myWorkerId == MAIN_PROCESS;
}

Wid __specpriv_num_workers(void)
{
  return numWorkers;
}

void __specpriv_set_pstage_replica_id(Wid rep_id)
{
  //DBG( "rep_id set to %u\n", rep_id );
  pstage_replica_id = rep_id;
}

Iteration __specpriv_last_committed(void)
{
  ParallelControlBlock *pcb = __specpriv_get_pcb();
  return pcb->checkpoints.main_checkpoint->iteration;
}

Iteration __specpriv_misspec_iter(void)
{
  ParallelControlBlock *pcb = __specpriv_get_pcb();
  assert( pcb->misspeculation_happened && "What? misspeculation didn't happen");

  return pcb->misspeculated_iteration;
}

void __specpriv_recovery_finished(Exit e)
{
  ParallelControlBlock *pcb = __specpriv_get_pcb();

  Iteration mi = pcb->misspeculated_iteration;

  pcb->misspeculation_happened = 0;
  pcb->checkpoints.main_checkpoint->iteration = mi;

  if( e > 0 )
    pcb->exit_taken = e;

  currentIter = mi + 1;

  DEBUG(printf("Recovery finished.  should resume from %u\n", currentIter));
}


uint32_t __specpriv_begin_invocation(void)
{
  TIME(main_begin_invocation);
  assert( myWorkerId == MAIN_PROCESS );

#if (AFFINITY & MP0INVOC) != 0
  // Set affinitiy: only processor zero
  cpu_set_t affinity;
  CPU_ZERO( &affinity );
  CPU_SET( CORE(0), &affinity );
  sched_setaffinity(0, sizeof(cpu_set_t), &affinity );
#endif

  // reset timers
  TOUT(
    ++InvocationNumber;
    worker_time_in_checkpoints=0;
    worker_time_in_priv_write=0;
    worker_time_in_priv_read=0;
    worker_time_in_io=0;
    numCheckpoints = 0;
    worker_private_bytes_read=0;
    worker_private_bytes_written=0;
  );

  ParallelControlBlock *pcb = __specpriv_get_pcb();
  pcb->misspeculation_happened = 0;
  pcb->exit_taken = 1;
  pcb->checkpoints.main_checkpoint->iteration = -1;

  __specpriv_fiveheaps_begin_invocation();

  currentIter = 0;

  DEBUG(printf("At beginning of invocation, sizeof(priv)=%u, sizeof(redux)=%u\n",
    __specpriv_sizeof_private(), __specpriv_sizeof_redux() ));

  return numWorkers;
}

// Spawn workers.  Returns worker ID
// if you are a child, or ~0UL if
// you are the parent.
Wid __specpriv_spawn_workers(Iteration firstIter)
{
  assert( myWorkerId == MAIN_PROCESS );

#if JOIN == SPIN
  ParallelControlBlock *pcb = __specpriv_get_pcb();
#endif

  Wid wid;

  // true by default
  runOnEveryIter = 1;

  for(wid=0; wid<numWorkers; ++wid)
  {
#if JOIN == SPIN
    // Child is not done yet.
    pcb->workerDoneFlags[ wid ] = 0;
#endif

    pid_t pid = fork();
    if( pid == 0 )
    {
      // child
      __specpriv_worker_starts(firstIter, wid);
      return wid;
    }

#if JOIN == WAITPID
    workerPids[ wid ] = pid;
#endif
  }

  return MAIN_PROCESS;
}

Exit __specpriv_join_children(void)
{
  TIME(worker_begin_waitpid);

  assert( myWorkerId == MAIN_PROCESS  );

  ParallelControlBlock *pcb = __specpriv_get_pcb();

#if JOIN == WAITPID
  for(Wid wid=0; wid<numWorkers; ++wid)
    waitpid( workerPids[wid], 0, 0 );
#endif

#if JOIN == SPIN
  // Wait until workers are almost done.
  // I.e. the front checkpoint will be the final one.
  struct timespec wt;
  for(;;)
  {
    if( pcb->misspeculation_happened )
      break;

    Checkpoint *front = pcb->checkpoints.used.first;
    if( front && front->iteration == LAST_ITERATION )
      break;

    // Sleep
    wt.tv_sec = 0;
    wt.tv_nsec = 1000000; // 1 millisecond
    nanosleep(&wt,0);

#if (WHO_DOES_CHECKPOINTS & FASTEST_WORKER) != 0
    __specpriv_commit_zero_or_more_checkpoints( & pcb->checkpoints );
#endif
  }

  // Wait until all workers are done.
  for(;;)
  {
    Bool allDone = 1;

    if( pcb->misspeculation_happened )
      break;

    for(Wid wid=0; wid<numWorkers; ++wid)
      if( !pcb->workerDoneFlags[wid] )
      {
        allDone = 0;
        break;
      }

    if( allDone )
      break;

    wt.tv_sec = 0;
    wt.tv_nsec = 1000; // 1 microsecond
    nanosleep(&wt,0);
  }
#endif

  TIME(worker_end_waitpid);

  TIME(distill_into_liveout_start);

  // Ensure that I am currently mounting the
  // non-speculative version of the private
  // and redux heaps.
  __specpriv_distill_checkpoints_into_liveout( &pcb->checkpoints );

  TIME(distill_into_liveout_end);

  if( pcb->misspeculation_happened )
    return 0;
  else
    return pcb->exit_taken;
}

Exit __specpriv_end_invocation(void)
{
#if (AFFINITY & MP0INVOC) != 0
  if( myWorkerId == MAIN_PROCESS )
    sched_setaffinity(0, sizeof(cpu_set_t), &old_affinity );
#endif

  TIME(main_end_invocation);

  TOUT(__specpriv_print_main_times());
  return __specpriv_get_pcb()->exit_taken;
}

// Called by a worker at the beginning of an iteration.
// A worker should call this during ALL iterations,
// even during those it does not execute.
void __specpriv_begin_iter(void)
{
  DEBUG(printf("iter %u %u\n", myWorkerId, currentIter));

  __specpriv_reset_local();
}

// Called by a worker at the end of an iteration.
// A worker should call this during ALL iterations,
// even during those it does not execute.
void __specpriv_end_iter(void)
{
  if( __specpriv_num_local() > 0 )
    __specpriv_misspec("Object lifetime misspeculation");

#if SIMULATE_MISSPEC != 0
  if( currentIter == simulateMisspeculationAtIter )
    __specpriv_misspec("Simulated misspeculation");
#endif

  ParallelControlBlock *pcb = __specpriv_get_pcb();
  if( pcb->misspeculation_happened )
    _exit(0);

  ++currentIter;
  Iteration globalCurIter = currentIter;
  if (!runOnEveryIter)
    globalCurIter = myWorkerId + (currentIter * numWorkers);

  //__specpriv_advance_iter(++currentIter);
  __specpriv_advance_iter(globalCurIter);
}

void __specpriv_final_iter_ckpt_check(uint64_t rem, uint64_t chunkSize) {
  uint64_t chunkedRem = rem / chunkSize;
  if (rem % chunkSize)
    ++chunkedRem;

  if (myWorkerId >= chunkedRem) {
    DEBUG(printf("worker_id:%u\n", myWorkerId));

    __specpriv_begin_iter();
    __specpriv_end_iter();
  }
}

// Perform a UO test.  Specifically, this ensures
// that certain bits within the pointer match the
// encoded heap information
void __specpriv_uo(void *ptr, uint64_t code, const char *msg)
{
  if( ptr )
    if( (POINTER_MASK & (uint64_t)ptr) != code )
      __specpriv_misspec(msg);
}

// -----------------------------------------------------------------------
// Value prediction

void __specpriv_predict(uint64_t observed, uint64_t expected)
{
  if( observed != expected )
    __specpriv_misspec("Value prediction failed");
}

//Wid __specpriv_spawn_workers_callback(Iteration firstIter, void (*callback)(void*), void *user)
Wid __specpriv_spawn_workers_callback(
    Iteration firstIter, void (*callback)(void *, int64_t, int64_t, int64_t),
    void *user, int64_t numCores, int64_t chunkSize)
{
  DEBUG(printf("spawn_workers_callback\n"));
  DEBUG(printf("numCores:%lu, chunkSize:%lu\n", numCores, chunkSize));

  Wid id = __specpriv_spawn_workers(firstIter);
  if( id == ~0U )
    return id; // parent

  // child
  //callback( user );
  callback( user, id, numCores, chunkSize);

 // Should never return.
  __specpriv_worker_finishes(0);
  return 0;
}


