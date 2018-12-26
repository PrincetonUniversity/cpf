#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "api.h"
#include "internals/affinity.h"
#include "internals/constants.h"
#include "internals/debug.h"
#include "internals/pcb.h"
#include "internals/private.h"
#include "internals/profile.h"
#include "internals/strategy.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/malloc.h"
#include "internals/smtx/packet.h"
#include "internals/smtx/prediction.h"
#include "internals/smtx/protection.h"
#include "internals/smtx/smtx.h"
#include "internals/smtx/units.h"

namespace specpriv_smtx
{

int32_t num_procs = 0;
int32_t num_aux_workers = 0;

// number of available workers

static int32_t num_available_workers = -1;
static int32_t num_active_workers = 0;

// main process keeps the pid of worker processes

static pid_t* worker_pids;

/*
 * Description:
 *
 *  Forks processes for parallel execution, including auxiliary processes, and initialize them
 *
 * Arguments:
 *
 *  num_workers - number of real workers which will be used to actually run the program
 *  num_aux_workers - number of auxiliary workers, like try-commit
 *  current_iter - current iteration count
 *
 * Return:
 *
 *  worker ID of the process
 */

static Wid spawn_workers(int32_t num_workers, int32_t n_aux_workers, Iteration current_iter)
{
#if DEBUG_ON
  fprintf(stderr, "spawn_workers called\n");
#endif
  Wid wid = 0;

  num_active_workers = num_workers;
  num_available_workers -= num_workers;
  worker_pids = (pid_t*)malloc( sizeof(pid_t) * (unsigned)(num_workers + n_aux_workers) );

  // performance debugging
#if (PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)
  init_profile();
#endif

  // notify try-commit worker id

  set_try_commit_begin( (Wid)num_workers );

  unsigned num_all_workers = (unsigned)(num_workers + n_aux_workers);

  for ( ; wid < num_all_workers ; wid++)
  {
    pid_t pid = fork();
    if ( pid == 0 )
    {
      // initial setup for worker process

      init_worker(current_iter, wid);

      return wid;
    }

    if (pid == -1)
    {
      perror("fork");
    }

    worker_pids[ wid ] = pid;
  }

  set_current_iter(current_iter);

  return MAIN_PROCESS_WID;
}

/*
 * Description:
 *
 *  Returns the number of workers that can be assigned to the execution itself. a
 *
 * Returns:
 *
 *  Number of available workers for program execution.
 *  (i.e. # of all avaiable workers - # of workers reserved for auxiliary work like try-commit)
 */

int32_t PREFIX(num_available_workers)(void)
{
  if ( num_available_workers == -1 )
  {
    char* nprocessors = getenv("NPROCESSORS");
    fprintf(stderr, "nprocessors %s\n", nprocessors);

    if ( nprocessors != NULL )
    {
      // nprocessors:
      // 1   for commnit
      // n   for try-commit
      // n*4 for workers;
#if 0
      int32_t nproc = atoi(nprocessors);
      int32_t n = (nproc - 1) / 10;
      num_aux_workers = n;
      num_available_workers = n>0?n*9:0;
#else
      num_procs = atoi(nprocessors);
      num_aux_workers = 1;
      num_available_workers = (num_procs - 2) > 0 ? (num_procs - 2) : 0;
#endif
    }
    else
      num_available_workers = 0;
  }

  return num_available_workers;
}

/*
 * Description:
 *
 *  Forks processes for parallel execution, including auxiliary processes, then make each process to
 *  call its own callback function
 *
 * Arguments:
 *
 *  current_iter - current iteration count
 *  callback - a callback function that every "real" worker processes should call
 *  user - a parameter for the callback function
 *
 * Return:
 *
 *  For main process, returns the wid of the main process. For others, shouldn't be returned.
 */

Wid PREFIX(spawn_workers_callback)(int32_t current_iter, void (*callback)(int8_t*), int8_t* user)
{
#if DEBUG_ON
  fprintf(stderr, "__specpriv_spawn_workers_callback, wid %u pid %u iter %d main_proc_wid %u\n", PREFIX(my_worker_id)(), getpid(), current_iter, MAIN_PROCESS_WID);
#endif

  // only main process can run this function

  assert( __specpriv_my_worker_id() == MAIN_PROCESS_WID );

  unsigned num_workers = (unsigned)__specpriv_num_available_workers();

  // debug

  init_debug(num_workers + (unsigned)num_aux_workers);

  // initialize queues and packets

  init_queues(num_workers,(unsigned) num_aux_workers);
  init_packets(num_workers + (unsigned)num_aux_workers);


  // TODO: should be move to compiler! set "good_to_go" 0

  (*good_to_go) = 0;

  // spawn workers

#if DEBUG_ON
  fprintf(stderr, "About to spawn workers\n");
#endif

  Wid id = spawn_workers( (int32_t)num_workers, num_aux_workers, current_iter );
  if ( id == MAIN_PROCESS_WID )
  {
    // set affinity

    return id;
  }

  // worker process calls callback function

#if DEBUG_ON
  fprintf(stderr, "__specpriv_spawn_workers_callback, worker %u, pid: %u calls callback function, numworkers: %u, num_aux_workers: %u\n", id, getpid(), num_workers, num_aux_workers);
#endif

  if ( id < num_workers )
  {
    // workers register special SIGSEGV handler
    register_handler();

#if PROFILE
    forward_time[id] = rdtsc() - execution_time[id];
#endif

    callback( user );             // workers for execution itself
  }
  else if ( id < (num_workers + (unsigned)num_aux_workers) )
  {
    try_commit();  // try commit process
  }
  else
    assert( false && "Invalid worker id\n" );

  PREFIX(worker_finishes)(0);

  // As __specpriv_worker_finishes terminates the process, should not reach here

  assert( false && "shouldn't be reached\n" );
  return 0;
}

Exit PREFIX(join_children)(void)
{
  DBG("__specpriv_join_children\n");

  // only main process can run this function

  assert( PREFIX(my_worker_id)() == MAIN_PROCESS_WID );

  // wait until all worker processes terminated and all commits performed

  commit( (uint32_t)num_active_workers+(uint32_t)num_aux_workers, worker_pids );

#if (PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)
  dump_profile(MAX_WORKERS);
#endif

#if (PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)
  fini_profile();
#endif
  free(worker_pids);

  DBG("__specpriv_join_children, all worker threads have joined\n");

  // TODO: move to compiler! reset prediction buffers
  reset_predictors();

  // free queues and packets

  fini_queues((unsigned)num_active_workers, (unsigned)num_aux_workers);
  fini_packets((unsigned)(num_active_workers+num_aux_workers));

  // reset try_commit_begin (wid of the first try-commit process)

  reset_try_commit_begin();

  num_available_workers += num_active_workers;
  num_active_workers = 0;

  PCB* pcb = get_pcb();

  Exit ret = 0;
  if( !pcb->misspeculation_happened )
    ret = pcb->exit_taken;

  DBG("__specpriv_join_children returns %u\n", ret);

  return ret;
}

}
