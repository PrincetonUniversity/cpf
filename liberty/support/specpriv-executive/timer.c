#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "timer.h"

// The INVOCATION number
unsigned InvocationNumber = 0;

// Timer variables -- per-invocation
uint64_t main_begin_invocation, main_end_invocation;
uint64_t worker_begin_invocation, worker_end_invocation;
uint64_t worker_enter_loop, worker_exit_loop;
uint64_t worker_begin_waitpid, worker_end_waitpid;
uint64_t distill_into_liveout_start, distill_into_liveout_end;

uint64_t produce_time = 0;
uint64_t consume_time = 0;
uint64_t produce_wait_time = 0;
uint64_t consume_wait_time = 0;
uint64_t produce_actual_time = 0;
uint64_t consume_actual_time = 0;

//////// ONE-TIME COSTS ////////
/******************************************************************************
 * Worker setup time
 */
uint64_t worker_setup_cpu_time = 0;

//////// PER-INVOCATION TIMES ////////
/******************************************************************************
 * Writing/reading from pipe time
 */
uint64_t main_write_pipe_time = 0;
uint64_t worker_read_pipe_time = 0;

/******************************************************************************
 * Worker total invocation time (after reading from pipe)
 */
uint64_t worker_total_invocation_time = 0;

  /******************************************************************************
   * Worker invocation setup time
   */
  uint64_t worker_setup_invocation_time = 0;

  /******************************************************************************
   * Worker actual loop time (after invocation setup)
   */
  uint64_t worker_loop_time = 0;

  /******************************************************************************
   * Worker on iteration time
   */
  uint64_t worker_on_iteration_time = 0;

  /******************************************************************************
   * Worker off iteration time
   */
  uint64_t worker_off_iteration_time = 0;

  /******************************************************************************
   * Worker time in private writes/reads
   */
  uint64_t worker_private_write_time = 0;
  uint64_t worker_private_read_time = 0;

  /******************************************************************************
   * Worker time in redux
   */
  uint64_t worker_redux_time = 0;

  /******************************************************************************
   * Worker time in io
   */
  uint64_t worker_io_time = 0;

  /******************************************************************************
   * Worker time in produces/consumes
   */
  uint64_t worker_produce_time = 0;
  uint64_t worker_consume_time = 0;

  /******************************************************************************
   * Worker time in checkpoints
   */
  uint64_t worker_checkpoint_time = 0;

/******************************************************************************
 * Other statistics
 */
uint64_t worker_number_produces = 0;
uint64_t worker_number_consumes = 0;
uint64_t worker_private_bytes_read = 0;
uint64_t worker_private_bytes_written = 0;

uint64_t pipe_read_time = 0;
uint64_t pipe_write_time = 0;

uint64_t get_queue_time = 0;

uint64_t worker_iteration_start;
uint64_t worker_set_iter_time = 0;
uint64_t worker_end_iter = 0;
uint64_t worker_between_iter_time = 0;
uint64_t worker_end_iter_checks = 0;
uint64_t worker_between_iteration_start = 0;
uint64_t worker_between_iteration_time = 0;
uint64_t worker_time_in_checkpoints=0;
uint64_t worker_final_checkpoint_time = 0;
uint64_t worker_intermediate_checkpoint_time = 0;
uint64_t worker_checkpoint_check_time = 0;
uint64_t worker_time_in_redux=0;
uint64_t worker_time_in_priv_write=0;
uint64_t worker_time_in_priv_read=0;
uint64_t worker_time_in_io=0;

uint64_t total_produces, total_consumes;

CheckpointRecord checkpoints[ MAX_CHECKPOINTS ];
unsigned numCheckpoints;

uint64_t rdtsc(void)
{
  uint32_t a, d;
  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) <<32 );
}

void __specpriv_print_percentages( void )
{
#if TIMER_PRINT_TIMELINE

  if ( __specpriv_i_am_main_process() )
    return;
  double total_time = (double) (worker_end_invocation - worker_begin_invocation);

  uint64_t worker_loop_time = worker_exit_loop - worker_enter_loop;
  uint64_t worker_useful_work_time = worker_loop_time - consume_time - produce_time
    - worker_time_in_checkpoints - worker_time_in_priv_read - worker_time_in_priv_write
    - worker_time_in_io;
  uint64_t worker_initialize_time = worker_enter_loop - worker_begin_invocation;

  printf("Total worker invocation time:   %18lu\n", worker_end_invocation - worker_begin_invocation);
  printf("Total worker loop time:         %18lu\n", worker_loop_time);
  printf("Worker loop initialize time:    %18lu\n", worker_enter_loop - worker_begin_invocation);
  printf("Total worker useful work time:  %18lu\n", worker_useful_work_time);
  printf("Total worker off iter time:     %18lu\n", worker_off_iteration_time);
  printf("Total worker on iter time:      %18lu\n", worker_on_iteration_time);
  printf("Time checking in end_iter():    %18lu\n", worker_end_iter_checks);
  printf("Time spent in checkpoints:      %18lu\n", worker_time_in_checkpoints);
  printf("Time in checking checkpoints:   %18lu\n", worker_checkpoint_check_time);
  printf("Time in intermediate ckpts:     %18lu\n", worker_intermediate_checkpoint_time);
  printf("Time in final checkpoint:       %18lu\n", worker_final_checkpoint_time);
  printf("Time spent in private reads:    %18lu\n", worker_time_in_priv_read);
  printf("Time spent in private writes:   %18lu\n", worker_time_in_priv_write);
  printf("Time spent in IO:               %18lu\n", worker_time_in_io);
  printf("Total number of produces:       %18lu\n", total_produces);
  printf("Total number of consume:        %18lu\n", total_consumes);
  printf("Produces took:                  %18lu\n", produce_time);
  printf("Time waiting for produce queue: %18lu\n", produce_wait_time);
  printf("Consumes took:                  %18lu\n", consume_time);
  printf("Time waiting for consume queue: %18lu\n", consume_wait_time);

  printf("\nPercentages spent in loop invocation\n");
  printf("Worker initialize:          %12lf\n"
         "Worker loop:                %12lf\n"
         "Worker useful work:         %12lf\n"
         "Worker off iteration:       %12lf\n"
         "Worker on iteration:        %12lf\n"
         "Worker between iteration:   %12lf\n"
         "Worker end_iter() checks:   %12lf\n"
         "Worker checkpoint:          %12lf\n"
         "Worker checkpoint check:    %12lf\n"
         "Worker intermediate ckpt:   %12lf\n"
         "Worker final ckpt:          %12lf\n"
         "Worker private reads:       %12lf\n"
         "Worker private writes:      %12lf\n"
         "Worker IO:                  %12lf\n"
         "Worker produces:            %12lf\n"
         "Worker waiting for produce: %12lf\n"
         "Worker actual produce:      %12lf\n"
         "Worker consumes:            %12lf\n"
         "Worker waiting for consume: %12lf\n"
         "Worker actual consume:      %12lf\n"
         "Worker getting queue:       %12lf\n",

         (double)(worker_initialize_time*100) / total_time,
         (double)(worker_loop_time*100) / total_time,
         (double)(worker_useful_work_time*100) / total_time,
         (double)(worker_off_iteration_time*100) / total_time,
         (double)(worker_on_iteration_time*100) / total_time,
         (double)(worker_between_iter_time*100) / total_time,
         (double)(worker_end_iter_checks*100) / total_time,
         (double)(worker_time_in_checkpoints*100) / total_time,
         (double)(worker_checkpoint_check_time*100) / total_time,
         (double)(worker_intermediate_checkpoint_time*100) / total_time,
         (double)(worker_final_checkpoint_time*100) / total_time,
         (double)(worker_time_in_priv_read*100) / total_time,
         (double)(worker_time_in_priv_write*100) / total_time,
         (double)(worker_time_in_io*100) / total_time,
         (double)(produce_time*100) / total_time,
         (double)(produce_wait_time*100) / total_time,
         (double)(produce_actual_time*100) / total_time,
         (double)(consume_time*100) / total_time,
         (double)(consume_wait_time*100) / total_time,
         (double)(consume_actual_time*100) / total_time,
         (double)(get_queue_time*100) / total_time
         );
#endif
}

void __specpriv_print_worker_times(void)
{
  const uint64_t myWorkerId = __specpriv_my_worker_id();

#if TIMER_PRINT_TIMELINE
  printf("WORKER %ld times\n", myWorkerId);
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

    printf(". %10ld w%ld + chkpt-pv[%u]\n", rec->private_start - main_begin_invocation, myWorkerId, i);
    printf(". %10ld w%ld - chkpt-pv[%u]\n", rec->private_stop - main_begin_invocation, myWorkerId, i);

    printf(". %10ld w%ld + chkpt-rx[%u]\n", rec->redux_start - main_begin_invocation, myWorkerId, i);
    printf(". %10ld w%ld - chkpt-rx[%u]\n", rec->redux_stop - main_begin_invocation, myWorkerId, i);

    printf(". %10ld w%ld + chkpt-io[%u]\n", rec->io_start - main_begin_invocation, myWorkerId, i);
    printf(". %10ld w%ld - chkpt-io[%u]\n", rec->io_stop - main_begin_invocation, myWorkerId, i);

    printf(". %10ld w%ld + chkpt-ioc[%u]\n", rec->io_commit_start - main_begin_invocation, myWorkerId, i);
    printf(". %10ld w%ld - chkpt-ioc[%u]\n", rec->io_commit_stop - main_begin_invocation, myWorkerId, i);

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
  printf("MAIN TIMES\n");
  printf("Time for main invocation: %12lu\n", main_end_invocation - main_begin_invocation);
  printf(
       ". %10ld m + invoc\n"
       ". %10ld m + waitpid\n"
       ". %10ld m - waitpid\n"
       ". %10ld m + io\n"
       ". %10ld m - io\n"
       ". %10ld m + distLO\n"
       ". %10ld m - distLO\n"
       ". %10ld m - invoc\n",

       main_begin_invocation - main_begin_invocation,
       worker_begin_waitpid - main_begin_invocation,
       worker_end_waitpid - main_begin_invocation,

       worker_end_waitpid - main_begin_invocation,
       worker_end_waitpid - main_begin_invocation + worker_time_in_io,

       distill_into_liveout_start - main_begin_invocation,
       distill_into_liveout_end - main_begin_invocation,

       main_end_invocation - main_begin_invocation);

#endif

#if TIMER_PRINT_OVERHEAD
  printf("@ invoc %d : MAIN +invoc %ld -invoc %ld\n",
    InvocationNumber, main_begin_invocation, main_end_invocation);
  fflush(stdout);
#endif
}


