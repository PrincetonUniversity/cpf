#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "timer.h"
#include "private.h"

/* #if TIMER != 0 */
/* uint64_t timers_hit = 0; */
/* #endif */

// The INVOCATION number
unsigned InvocationNumber = 0;

// Timer variables -- per-invocation
uint64_t worker_enter_loop, worker_exit_loop;
uint64_t worker_begin_waitpid, worker_end_waitpid;
uint64_t distill_into_liveout_start, distill_into_liveout_end;

uint64_t get_queue_time = 0;
uint64_t produce_time = 0;
uint64_t consume_time = 0;
uint64_t produce_wait_time = 0;
uint64_t consume_wait_time = 0;
uint64_t produce_actual_time = 0;
uint64_t consume_actual_time = 0;

//validation costs
uint64_t var_uo_check_time = 0;
uint64_t var_uo_check_count = 0;
uint64_t var_private_write_time = 0;
uint64_t var_private_write_count = 0;
uint64_t var_private_read_time = 0;
uint64_t var_private_read_count = 0;


//////// ONE-TIME COSTS ////////
/******************************************************************************
 * Worker time from when __specpriv_worker_setup() is called to worker.callback()
 */
uint64_t worker_setup_time = 0; // done

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
   * Worker iteration times
   */
  uint64_t worker_on_iteration_time = 0;
  uint64_t worker_off_iteration_time = 0;
  uint64_t worker_between_iter_time = 0;

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
  uint64_t worker_intermediate_io_time = 0; // XXX should be none in off iteration?
  uint64_t worker_copy_io_to_redux_time = 0;
  uint64_t worker_commit_io_time = 0;

  /******************************************************************************
   * Worker time in produces/consumes
   */
  uint64_t worker_produce_time = 0;
  uint64_t worker_consume_time = 0;

  /******************************************************************************
   * Worker time in checkpoints
   */
  uint64_t worker_final_checkpoint_time = 0;
  uint64_t worker_intermediate_checkpoint_time = 0;
  uint64_t worker_checkpoint_check_time = 0;

  /******************************************************************************
   * Worker redux times
   */
  uint64_t worker_distill_redux_into_partial_time = 0;
  uint64_t worker_distill_ommitted_redux_into_partial_time = 0;

/******************************************************************************
 * Other statistics
 */
  uint64_t worker_number_produces = 0;
  uint64_t worker_number_consumes = 0;
  uint64_t worker_private_bytes_read = 0;
  uint64_t worker_private_bytes_written = 0;

/******************************************************************************
 * Miscellaneous temporaries
 */
  uint64_t worker_pause_time = 0;
  uint64_t worker_begin_iter_time = 0;
  uint64_t worker_end_iter_time = 0;
  uint64_t main_begin_invocation = 0;
  uint64_t main_end_invocation = 0;
  uint64_t worker_begin_invocation = 0;
  uint64_t worker_end_invocation = 0;

/******************************************************************************
 * Hardcore debugging timers
 */
  uint64_t worker_prepare_checkpointing_time = 0;
  uint64_t worker_unprepare_checkpointing_time = 0;
  uint64_t worker_redux_to_partial_time = 0;
  uint64_t worker_committed_redux_to_partial_time = 0;
  uint64_t worker_redux_to_main_time = 0;
  uint64_t worker_private_to_partial_time = 0;
  uint64_t worker_committed_private_to_partial_time = 0;
  uint64_t worker_killpriv_to_partial_time = 0;
  uint64_t worker_committed_killpriv_to_partial_time = 0;
  uint64_t worker_acquire_lock_time = 0;

  uint64_t priv_read_times_array[1000000];
  uint64_t priv_write_times_array[1000000];


uint64_t total_produces, total_consumes;

CheckpointRecord checkpoints[ MAX_CHECKPOINTS ];
unsigned numCheckpoints;

// gc14 -- using rdtscp instead of rdtsc for more accurate timings
uint64_t rdtsc(void)
{
  uint32_t a, d;
  /* timers_hit++; */
  //__asm__ volatile("rdtscp" : "=a" (a), "=d" (d));
  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) <<32 );
}

static uint64_t cycles_per_second = 0;
uint64_t countCyclesPerSecond(void) {
  if (cycles_per_second > 0)
    return cycles_per_second;

  struct timeval tv_start, tv_stop;
  gettimeofday(&tv_start, 0);
  const uint64_t start = rdtsc();

  sleep(10);

  const uint64_t stop = rdtsc();
  gettimeofday(&tv_stop, 0);

  double actual_duration = (tv_stop.tv_sec - tv_start.tv_sec) +
                           (tv_stop.tv_usec - tv_start.tv_usec) * 1.0e-9;

  cycles_per_second = (stop - start) / (actual_duration);
  printf("There are %lu cycles/second\n", cycles_per_second);
  return cycles_per_second;
}

void __specpriv_reset_timers(void)
{
    ++InvocationNumber;
    numCheckpoints = 0;
    worker_read_pipe_time = 0;
    worker_setup_invocation_time = 0;
    worker_on_iteration_time = 0;
    worker_off_iteration_time = 0;
    worker_private_read_time = 0;
    worker_private_write_time = 0;
    worker_intermediate_io_time = 0;
    worker_checkpoint_check_time = 0;
    worker_between_iter_time = 0;
    worker_intermediate_checkpoint_time = 0;
    worker_final_checkpoint_time = 0;
    worker_copy_io_to_redux_time = 0;
    worker_commit_io_time = 0;
    worker_private_bytes_read = 0;
    worker_private_bytes_written = 0;
    worker_end_iter_time = 0;
    /* timers_hit = 0; */
    worker_prepare_checkpointing_time = 0;
    worker_unprepare_checkpointing_time = 0;
    worker_acquire_lock_time = 0;
    worker_redux_to_partial_time = 0;
    worker_committed_redux_to_partial_time = 0;
    worker_redux_to_main_time = 0; // this is run in main so won't see shit in workers
    worker_private_to_partial_time = 0;
    worker_committed_private_to_partial_time = 0;
    worker_killpriv_to_partial_time = 0;
    worker_committed_killpriv_to_partial_time = 0;

    for ( int i = 0; i < 1000000; i++ )
    {
      priv_read_times_array[i] = 0;
      priv_write_times_array[i] = 0;
    }
}

void __specpriv_add_right_time( uint64_t *on_time, uint64_t *off_time, uint64_t begin )
{
  if ( begin == 0 )
    return;
  if ( __specpriv_is_on_iter() )
  {
    TADD(*on_time, begin);
  }
  else
    TADD(*off_time, begin);
}

void __specpriv_print_percentages( void )
{
#if TIMER_PRINT_TIMELINE | TIMER

  if ( __specpriv_i_am_main_process() )
    return;
  const uint64_t myWorkerId = __specpriv_my_worker_id();

  uint64_t total_iteration_time = worker_on_iteration_time + worker_off_iteration_time +
    worker_private_read_time + worker_private_write_time + worker_intermediate_io_time;
  /* uint64_t total_invocation_time = total_iteration_time + worker_setup_invocation_time + */
  /*   worker_final_checkpoint_time + worker_between_iter_time; */
  uint64_t total_invocation_time = total_iteration_time + worker_setup_invocation_time +
    worker_final_checkpoint_time + worker_between_iter_time;

  printf("\n*** WORKER %ld @invocation %d times ***\n", myWorkerId, InvocationNumber);

  printf("Complete worker invocation time:          %15lu\n", worker_end_invocation - worker_begin_invocation);
  printf("Worker cpu set setup time:                %15lu\n", worker_setup_time);
  printf("Worker read pipe time:                    %15lu\n", worker_read_pipe_time);
  printf("Worker invocation time:                   %15lu\n", total_invocation_time);
  printf("-- Worker invocation setup time:          %15lu\n", worker_setup_invocation_time);
  printf("-- Worker total iteration time:           %15lu\n", total_iteration_time);
  printf("---- Worker on iteration time:            %15lu\n", worker_on_iteration_time);
  printf("---- Worker off iteration time:           %15lu\n", worker_off_iteration_time);
  printf("---- Worker time in private read:         %15lu\n", worker_private_read_time);
  printf("---- Worker time in private write:        %15lu\n", worker_private_write_time);
  printf("---- Worker time in intermediate IO:      %15lu\n", worker_intermediate_io_time);
  printf("-- Worker between iteration time:         %15lu\n", worker_between_iter_time);
  printf("---- Worker intermediate checkpoint time: %15lu\n", worker_intermediate_checkpoint_time);
  printf("------ Worker prepare checkpoint time:    %15lu\n", worker_prepare_checkpointing_time);
  printf("------ Worker acquire lock time:          %15lu\n", worker_acquire_lock_time);
  printf("------ Worker redux to partial:           %15lu\n", worker_redux_to_partial_time);
  printf("------ Committed redux to partial:        %15lu\n", worker_committed_redux_to_partial_time);
  printf("------ Worker private to partial:         %15lu\n", worker_private_to_partial_time);
  printf("------ Committed private to partial:      %15lu\n", worker_committed_private_to_partial_time);
  printf("------ Worker killpriv to partial:        %15lu\n", worker_killpriv_to_partial_time);
  printf("------ Committed killpriv to partial:     %15lu\n", worker_committed_killpriv_to_partial_time);
  printf("------ Worker unprepare checkpoint time:  %15lu\n", worker_unprepare_checkpointing_time);
  printf("-- Worker final checkpoint time:          %15lu\n", worker_final_checkpoint_time);
  printf("-- Worker time in copy io to redux:       %15lu\n", worker_copy_io_to_redux_time);
  printf("-- Worker time in commit io:              %15lu\n", worker_commit_io_time);
  printf("-- Distill redux to main:                 %15lu\n", worker_redux_to_main_time);

  printf("//// WORKER %ld percentages ////\n", myWorkerId);
  printf("Worker invocation:                        %15lf\n"
         "-- Worker setup:                          %15lf\n"
         "-- Worker total iteration:                %15lf\n"
         "---- Worker on iter:                      %15lf\n"
         "---- Worker off iter:                     %15lf\n"
         "---- Worker private read:                 %15lf\n"
         "---- Worker private write:                %15lf\n"
         "---- Worker intermediate IO:              %15lf\n"
         "---- Worker checkpoint check:             %15lf\n"
         "-- Worker between iteration:              %15lf\n"
         "---- Worker intermediate checkpoint:      %15lf\n"
         "-- Worker final checkpoint:               %15lf\n"
         "-- Worker time in copy io to redux:       %15lf\n"
         "-- Worker time in commit io:              %15lf\n",

         (double)(total_invocation_time*100) / total_invocation_time,
         (double)(worker_setup_invocation_time*100) / total_invocation_time,
         (double)(total_iteration_time*100) / total_invocation_time,
         (double)(worker_on_iteration_time*100) / total_invocation_time,
         (double)(worker_off_iteration_time*100) / total_invocation_time,
         (double)(worker_private_read_time*100) / total_invocation_time,
         (double)(worker_private_write_time*100) / total_invocation_time,
         (double)(worker_intermediate_io_time*100) / total_invocation_time,
         (double)(worker_checkpoint_check_time*100) / total_invocation_time,
         (double)(worker_between_iter_time*100) / total_invocation_time,
         (double)(worker_intermediate_checkpoint_time*100) / total_invocation_time,
         (double)(worker_final_checkpoint_time*100) / total_invocation_time,
         (double)(worker_copy_io_to_redux_time*100) / total_invocation_time,
         (double)(worker_commit_io_time*100) / total_invocation_time
      );

  printf("//// WORKER %ld other stats ////\n", myWorkerId);
  printf("First iteration:                          %15d\n",    __specpriv_get_first_iter());
  printf("Number of checkpoints:                    %15u\n",  numCheckpoints);
  printf("Number of private bytes written:          %15lu\n", worker_private_bytes_written);
  printf("Number of private bytes read:             %15lu\n", worker_private_bytes_read);
  for ( int i = 0; i < 1000000; i++ )
  {
    if ( priv_read_times_array[i] != 0 )
      printf("Private_read_time[%lu]: %lu\n",  i, priv_read_times_array[i]);
    if ( priv_write_times_array[i] != 0 )
      printf("Private_write_time[%lu]: %lu\n", i, priv_write_times_array[i]);
  }
  printf("*** END WORKER %ld @invocation %d times ***\n",   myWorkerId, InvocationNumber);
  fflush(stdout);
#endif
}

void __specpriv_print_worker_times(void)
{
  const uint64_t myWorkerId = __specpriv_my_worker_id();

#if TIMER_PRINT_TIMELINE_OLD
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

#if 0
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
#endif

}

void __specpriv_print_main_times(void)
{
#if 0
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
#endif
}

void __specpriv_reset_val_times(void) {
  var_uo_check_time = 0;
  var_uo_check_count = 0;
  var_private_write_time = 0;
  var_private_write_count = 0;
  var_private_read_time = 0;
  var_private_read_count = 0;
}

void __specpriv_print_val_times(void) {

  printf("UO check average cycle latency:      %15lf sec\n"
         "Private read average cycle latency:  %15lf sec\n"
         "Private write average cycle latency: %15lf sec\n",
         ((double)var_uo_check_time) /
             (var_uo_check_count * (double)countCyclesPerSecond()),
         ((double)var_private_read_time) /
             (var_private_read_count * (double)countCyclesPerSecond()),
         ((double)var_private_write_time) /
             (var_private_write_count * (double)countCyclesPerSecond()));

  fflush(stdout);
}
