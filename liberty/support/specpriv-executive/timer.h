#ifndef LLVM_LIBERTY_SPEC_PRIV_TIMER_H
#define LLVM_LIBERTY_SPEC_PRIV_TIMER_H

#include <stdint.h>

#include "constants.h"
#include "config.h"

uint64_t rdtsc(void);

#if TIMER != 0

/* extern uint64_t timers_hit; */
#define TOUT(...)   do { __VA_ARGS__ ; } while(0)
#define TIME(v)     do { v = rdtsc() ; } while(0)
#define TADD(d,s)   do { d += rdtsc() - s; } while(0)

#else

#define TOUT(...)
#define TIME(v)     do { (void)v; } while(0)
#define TADD(d,s)   do { (void)d; (void)s; } while(0)

#endif

void __specpriv_reset_timers(void);
void __specpriv_add_right_time( uint64_t *on_time, uint64_t *off_time, uint64_t begin );
void __specpriv_print_worker_times(void);
void __specpriv_print_main_times(void);
void __specpriv_print_percentages( void );

// Timer variables
extern uint64_t main_begin_invocation, main_end_invocation;
extern uint64_t worker_begin_invocation, worker_end_invocation;
extern uint64_t worker_enter_loop, worker_exit_loop;
extern uint64_t worker_begin_waitpid, worker_end_waitpid;
extern uint64_t distill_into_liveout_start, distill_into_liveout_end;

//////// ONE-TIME COSTS ////////
/******************************************************************************
 * Worker setup time
 */
extern uint64_t worker_setup_time;

//////// PER-INVOCATION TIMES ////////
/******************************************************************************
 * Writing/reading from pipe time
 */
extern uint64_t main_write_pipe_time;
extern uint64_t worker_read_pipe_time;

/******************************************************************************
 * Worker total invocation time (after reading from pipe)
 */
extern uint64_t worker_total_invocation_time;

  /******************************************************************************
   * Worker invocation setup time
   */
  extern uint64_t worker_setup_invocation_time;

  /******************************************************************************
   * Worker actual loop time (after invocation setup)
   */
  extern uint64_t worker_loop_time;

  /******************************************************************************
   * Worker iteration times
   */
  extern uint64_t worker_on_iteration_time;
  extern uint64_t worker_off_iteration_time;
  extern uint64_t worker_between_iter_time;

  /******************************************************************************
   * Worker time in private writes/reads
   */
  extern uint64_t worker_private_write_time;
  extern uint64_t worker_private_read_time;
  extern uint64_t num_private_reads;
  extern uint64_t num_private_writes;

  /******************************************************************************
   * Worker time in redux
   */
  extern uint64_t worker_redux_time;

  /******************************************************************************
   * Worker time in io
   */
  extern uint64_t worker_intermediate_io_time;
  extern uint64_t worker_copy_io_to_redux_time;
  extern uint64_t worker_commit_io_time;

  /******************************************************************************
   * Worker time in produces/consumes
   */
  extern uint64_t worker_produce_time;
  extern uint64_t worker_consume_time;

  /******************************************************************************
   * Worker time in checkpoints
   */
  extern uint64_t worker_final_checkpoint_time;
  extern uint64_t worker_intermediate_checkpoint_time;
  extern uint64_t worker_checkpoint_check_time;

/******************************************************************************
 * Other statistics
 */
  extern uint64_t worker_number_produces;
  extern uint64_t worker_number_consumes;
  extern uint64_t worker_private_bytes_read;
  extern uint64_t worker_private_bytes_written;

/******************************************************************************
 * Miscellaneous temporaries
 */
  extern uint64_t worker_pause_time;
  extern uint64_t worker_begin_iter_time;
  extern uint64_t worker_end_iter_time;

/******************************************************************************
 * Hardcore debugging timers
 */
  extern uint64_t worker_prepare_checkpointing_time;
  extern uint64_t worker_unprepare_checkpointing_time;
  extern uint64_t worker_redux_to_partial_time;
  extern uint64_t worker_committed_redux_to_partial_time;
  extern uint64_t worker_redux_to_main_time;
  extern uint64_t worker_private_to_partial_time;
  extern uint64_t worker_committed_private_to_partial_time;
  extern uint64_t worker_killpriv_to_partial_time;
  extern uint64_t worker_committed_killpriv_to_partial_time;
  extern uint64_t worker_acquire_lock_time;

  extern uint64_t priv_read_times_array[1000000];
  extern uint64_t priv_write_times_array[1000000];

extern uint64_t get_queue_time;
extern uint64_t produce_time;
extern uint64_t consume_time;
extern uint64_t produce_wait_time;
extern uint64_t consume_wait_time;
extern uint64_t produce_actual_time;
extern uint64_t consume_actual_time;


extern uint64_t total_produces, total_consumes;

extern uint64_t worker_private_bytes_read;
extern uint64_t worker_private_bytes_written;

extern unsigned InvocationNumber;

struct s_checkpoint_record
{
  uint64_t    checkpoint_start;
  uint64_t    checkpoint_stop;

  uint64_t    redux_start;
  uint64_t    redux_stop;

  uint64_t    private_start;
  uint64_t    private_stop;

  uint64_t    io_start;
  uint64_t    io_stop;

  uint64_t    io_commit_start;
  uint64_t    io_commit_stop;
};
typedef struct s_checkpoint_record CheckpointRecord;

extern CheckpointRecord checkpoints[ MAX_CHECKPOINTS ];
extern unsigned numCheckpoints;


#endif

