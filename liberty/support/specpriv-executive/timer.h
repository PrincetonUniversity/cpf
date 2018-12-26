#ifndef LLVM_LIBERTY_SPEC_PRIV_TIMER_H
#define LLVM_LIBERTY_SPEC_PRIV_TIMER_H

#include <stdint.h>

#include "constants.h"
#include "config.h"

uint64_t rdtsc(void);

#if TIMER != 0

#define TOUT(...)   do { __VA_ARGS__ ; } while(0)
#define TIME(v)     do { v = rdtsc() ; } while(0)
#define TADD(d,s)   do { d += rdtsc() - s; } while(0)

#else

#define TOUT(...)
#define TIME(v)     do { (void)v; } while(0)
#define TADD(d,s)   do { (void)d; (void)s; } while(0)

#endif

void __specpriv_print_worker_times(void);
void __specpriv_print_main_times(void);

// Timer variables
extern uint64_t main_begin_invocation, main_end_invocation;
extern uint64_t worker_begin_invocation, worker_end_invocation;
extern uint64_t worker_enter_loop, worker_exit_loop;
extern uint64_t worker_begin_waitpid, worker_end_waitpid;

#if JOIN == SPIN
extern uint64_t main_begin_waitpid_spin_slow, main_end_waitpid_spin_slow;
extern uint64_t main_begin_waitpid_spin_fast, main_end_waitpid_spin_fast;
#endif

extern uint64_t worker_time_in_checkpoints;
extern uint64_t worker_time_in_priv_write;
extern uint64_t worker_time_in_priv_read;
extern uint64_t worker_time_in_io;

extern uint64_t worker_private_bytes_read;
extern uint64_t worker_private_bytes_written;


struct s_checkpoint_record
{
  uint64_t    checkpoint_start;
  uint64_t    checkpoint_stop;

  uint64_t    find_checkpoint_start;
  uint64_t    find_checkpoint_stop;

  uint64_t    acquire_lock_start;
  uint64_t    acquire_lock_stop;

  uint64_t    map_start;
  uint64_t    map_stop;

  uint64_t    redux_start;
  uint64_t    redux_stop;

  uint64_t    private_start;
  uint64_t    private_stop;

  uint64_t    io_start;
  uint64_t    io_stop;

  uint64_t    io_commit_start;
  uint64_t    io_commit_stop;

  uint64_t    unmap_start;
  uint64_t    unmap_stop;

  uint64_t    combine_start;
  uint64_t    combine_stop;
};
typedef struct s_checkpoint_record CheckpointRecord;

extern CheckpointRecord checkpoints[ MAX_CHECKPOINTS ];
extern unsigned numCheckpoints;


#endif

