#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "api.h"
#include "internals/affinity.h"
#include "internals/debug.h"
#include "internals/pcb.h"
#include "internals/private.h"
#include "internals/profile.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/smtx.h"

namespace specpriv_smtx
{

/*
 * worker process private variables
 */

// for main process, which spawns the worker processes and commits the valid iteration,
// current_iteration is the "global" loop iteration count, while for worker processes
// current_iteration is a "local" loop iteration count.

static Iteration current_iteration = 0;

// worker id for each parallel worker. initislized to MAIN_PROCESS_WID, and updated once worker
// process spawned.

static Wid worker_id = MAIN_PROCESS_WID;

// parallel stage replica id

static Wid pstage_replica_id = NOT_A_PARALLEL_STAGE;

/*
 * Non-api functions
 */

// worker process initialization

void init_worker(Iteration current_iter, Wid wid)
{
#if PROFILE
  execution_time[wid] = rdtsc();
#endif
  DBG("init_worker, pid %u, wid: %u, iter %d\n", getpid(), wid, current_iter);

  assert( wid != MAIN_PROCESS_WID );
  worker_id = wid;

  current_iteration = current_iter;

  // affinity optimization

  // First, a policy to make the spawn work better
  sleep(0);

  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  CPU_SET( CORE( (wid+1) % num_procs ), &affinity );
  sched_setaffinity( 0, sizeof(cpu_set_t), &affinity );

  // TODO: handling i/o

  // TODO: handling signals

}

// worker id manipulation

Wid get_pstage_replica_id()
{
  return pstage_replica_id;
}

// iteration manipulation

void reset_current_iter()
{
  current_iteration = 0;
}

void advance_iter()
{
  current_iteration += 1;
}

void set_current_iter(Iteration iter)
{
  current_iteration = iter;
}

/*
 * Api functions
 */

// worker process wrapup

void PREFIX(worker_finishes)(Exit exittaken)
{
#if PROFILE
  memset_time[worker_id] = rdtsc();
#endif

  DBG( "worker_finishes, exitcode %d\n", exittaken);

  // this is for worker processes. main process shouldn't be terminated.

  assert( worker_id != MAIN_PROCESS_WID );

  // update exit_taken

  if ( worker_id < try_commit_begin )
  {
    PCB* pcb = get_pcb();
    if (pcb->exit_taken) {
      //fprintf(stderr, "worker_id %u\n", worker_id);
      //sot: TODO: This assertion can be triggered if a parallel stage somehow reached this point before a sequential stage. Maybe this assertion needs to be removed.
      // for now avoid the assertion if worker_id = 0, if this is PS-DSWP the first worker thread will be a sequential stage.
      if (worker_id != 0)
        assert( pcb->exit_taken == exittaken );
    }
    else
      pcb->exit_taken = exittaken;
  }

  // notify end of worker

  DBG("sending EOW packet\n");
  broadcast_event( worker_id, (void*)0xDEADBEEF, 0, NULL, WRITE, EOW );

  // flush queues for incoming uncommitted values so the previous stages can make progress thereby
  // see the misspec happened

  clear_incoming_queues( worker_id );

#if (PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)
  dump_profile(worker_id);
#endif

  _exit(0);
}

// worker id manipulation

Wid PREFIX(my_worker_id)()
{
#if SEQMODE
  return 0;
#else
  return worker_id;
#endif
}

void PREFIX(set_pstage_replica_id)(Wid rep_id)
{
  DBG( "rep_id set to %u\n", rep_id );
  pstage_replica_id = rep_id;
}

Iteration PREFIX(current_iter)(void)
{
  return current_iteration;
}

}
