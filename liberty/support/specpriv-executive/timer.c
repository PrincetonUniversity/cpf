#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "timer.h"

// Timer variables -- per-invocation
uint64_t main_begin_invocation, main_end_invocation;
uint64_t worker_begin_invocation, worker_end_invocation;
uint64_t worker_enter_loop, worker_exit_loop;
uint64_t worker_begin_waitpid, worker_end_waitpid;

#if JOIN == SPIN
uint64_t main_begin_waitpid_spin_slow, main_end_waitpid_spin_slow;
uint64_t main_begin_waitpid_spin_fast, main_end_waitpid_spin_fast;
#endif

uint64_t worker_time_in_checkpoints=0;
uint64_t worker_time_in_redux=0;
uint64_t worker_time_in_priv_write=0;
uint64_t worker_time_in_priv_read=0;
uint64_t worker_time_in_io=0;

uint64_t worker_private_bytes_read=0;
uint64_t worker_private_bytes_written=0;

CheckpointRecord checkpoints[ MAX_CHECKPOINTS ];
unsigned numCheckpoints;

uint64_t rdtsc(void)
{
  uint32_t a, d;
  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) <<32 );
}

void __specpriv_print_worker_times(void)
{
  const uint64_t myWorkerId = __specpriv_my_worker_id();

#if TIMER_PRINT_TIMELINE
  printf(". %10ld w%ld + invoc\n"
         ". %10ld w%ld + loop\n"
         ". %10ld w%ld - loop\n",
         worker_begin_invocation - main_begin_invocation, myWorkerId,
         worker_enter_loop - main_begin_invocation, myWorkerId,
         worker_exit_loop - main_begin_invocation, myWorkerId);

/*
  printf(". %10ld w%ld + waitpid\n"
         ". %10ld w%ld - waitpid\n",
         worker_begin_waitpid - main_begin_invocation, myWorkerId,
         worker_end_waitpid - main_begin_invocation, myWorkerId);
*/

  printf(". %10ld w%ld - invoc\n",
         worker_end_invocation - main_begin_invocation, myWorkerId);

  printf(". %10ld w%ld + priv-w\n"
         ". %10ld w%ld - priv-w\n"
         ". %10ld w%ld + priv-r\n"
         ". %10ld w%ld - priv-r\n",
         worker_enter_loop - main_begin_invocation , myWorkerId,
         worker_enter_loop - main_begin_invocation + worker_time_in_priv_write, myWorkerId,

         worker_enter_loop - main_begin_invocation + worker_time_in_priv_write, myWorkerId,
         worker_enter_loop - main_begin_invocation + worker_time_in_priv_write + worker_time_in_priv_read, myWorkerId);

  printf(". %10ld w%ld + io\n"
         ". %10ld w%ld - io\n"
         ": w%ld priv read %ld\n"
         ": w%ld priv write %ld\n",
         worker_enter_loop - main_begin_invocation + worker_time_in_priv_write + worker_time_in_priv_read, myWorkerId,
         worker_enter_loop - main_begin_invocation + worker_time_in_priv_write + worker_time_in_priv_read + worker_time_in_io, myWorkerId,

         myWorkerId, worker_private_bytes_read,
         myWorkerId, worker_private_bytes_written);

  unsigned i;
  for(i=0; i<numCheckpoints; ++i)
  {
    CheckpointRecord *rec = &checkpoints[i];
    printf(". %10ld w%ld + chkpt[%u]\n", rec->checkpoint_start - main_begin_invocation, myWorkerId, i);

		printf(". %10ld w%ld + chkpt-find[%u]\n", rec->find_checkpoint_start - main_begin_invocation, myWorkerId, i);
		printf(". %10ld w%ld - chkpt-find[%u]\n", rec->find_checkpoint_stop - main_begin_invocation, myWorkerId, i);

		printf(". %10ld w%ld + chkpt-lock[%u]\n", rec->acquire_lock_start - main_begin_invocation, myWorkerId, i);
		printf(". %10ld w%ld - chkpt-lock[%u]\n", rec->acquire_lock_stop - main_begin_invocation, myWorkerId, i);

		printf(". %10ld w%ld + chkpt-map[%u]\n", rec->map_start - main_begin_invocation, myWorkerId, i);
		printf(". %10ld w%ld - chkpt-map[%u]\n", rec->map_stop - main_begin_invocation, myWorkerId, i);

    printf(". %10ld w%ld + chkpt-pv[%u]\n", rec->private_start - main_begin_invocation, myWorkerId, i);
    printf(". %10ld w%ld - chkpt-pv[%u]\n", rec->private_stop - main_begin_invocation, myWorkerId, i);

    printf(". %10ld w%ld + chkpt-rx[%u]\n", rec->redux_start - main_begin_invocation, myWorkerId, i);
    printf(". %10ld w%ld - chkpt-rx[%u]\n", rec->redux_stop - main_begin_invocation, myWorkerId, i);

    printf(". %10ld w%ld + chkpt-io[%u]\n", rec->io_start - main_begin_invocation, myWorkerId, i);
    printf(". %10ld w%ld - chkpt-io[%u]\n", rec->io_stop - main_begin_invocation, myWorkerId, i);

    printf(". %10ld w%ld + chkpt-ioc[%u]\n", rec->io_commit_start - main_begin_invocation, myWorkerId, i);
    printf(". %10ld w%ld - chkpt-ioc[%u]\n", rec->io_commit_stop - main_begin_invocation, myWorkerId, i);

		printf(". %10ld w%ld + chkpt-unmap[%u]\n", rec->unmap_start - main_begin_invocation, myWorkerId, i);
		printf(". %10ld w%ld - chkpt-unmap[%u]\n", rec->unmap_stop - main_begin_invocation, myWorkerId, i);

		printf(". %10ld w%ld + chkpt-combine[%u]\n", rec->combine_start - main_begin_invocation, myWorkerId, i);
		printf(". %10ld w%ld - chkpt-combine[%u]\n", rec->combine_stop - main_begin_invocation, myWorkerId, i);

    printf(". %10ld w%ld - chkpt[%u]\n", rec->checkpoint_stop - main_begin_invocation, myWorkerId, i);
  }
#endif

#if TIMER_PRINT_OVERHEAD
  printf("@ invoc %d : WORKER %ld +invoc %ld -invoc %ld "
            "num_check %d time_check %ld "
            "num_priv_write %ld time_priv_write %ld "
            "num_priv_read %ld time_private_read %ld\n",
    InvocationNumber, myWorkerId,
    worker_begin_invocation,
    worker_end_invocation,
    numCheckpoints,
    worker_time_in_checkpoints,
    worker_private_bytes_written, worker_time_in_priv_write,
    worker_private_bytes_read, worker_time_in_priv_read);
  fflush(stdout);
#endif

}

void __specpriv_print_main_times(void)
{
#if TIMER_PRINT_TIMELINE
  printf(
       ". %10ld m + invoc\n"
       ". %10ld m + waitpid\n"
       ". %10ld m + spin-slow\n"
       ". %10ld m - spin-slow\n"
       ". %10ld m + spin-fast\n"
       ". %10ld m - spin-fast\n"
       ". %10ld m - waitpid\n"
       ". %10ld m + io\n"
       ". %10ld m - io\n"
       ". %10ld m - invoc\n",

       main_begin_invocation - main_begin_invocation,
       worker_begin_waitpid - main_begin_invocation,

       main_begin_waitpid_spin_slow - main_begin_invocation,
       main_end_waitpid_spin_slow - main_begin_invocation,
       main_begin_waitpid_spin_fast - main_begin_invocation,
       main_end_waitpid_spin_fast - main_begin_invocation,

       worker_end_waitpid - main_begin_invocation,

       worker_end_waitpid - main_begin_invocation,
       worker_end_waitpid - main_begin_invocation + worker_time_in_io,

       main_end_invocation - main_begin_invocation);
#endif

#if TIMER_PRINT_OVERHEAD
  printf("@ invoc %d : MAIN +invoc %ld -invoc %ld\n",
    InvocationNumber, main_begin_invocation, main_end_invocation);
  fflush(stdout);
#endif
}


