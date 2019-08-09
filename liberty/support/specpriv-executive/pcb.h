#ifndef LLVM_LIBERTY_SPEC_PRIV_EXECUTIVE_PCB_H
#define LLVM_LIBERTY_SPEC_PRIV_EXECUTIVE_PCB_H

#include "constants.h"
#include "types.h"
#include "io.h"
#include "config.h"
#include "checkpoint.h"

// Shared state for the parallel region (to be allocated in shared heap)
struct s_parallel_control_block
{
  // If the workers have finished, which
  // exit did the first-to-exit take?
  Exit                exit_taken;

  // Has misspeculation occurred?
  // If so, who, when, and why?
  Bool                misspeculation_happened;
  Wid                 misspeculated_worker;
  Iteration           misspeculated_iteration;
  const char *        misspeculation_reason;

  // in case of misspeculataion, what was the last checkpoint committed
  Checkpoint *        last_committed;

  char padding1[128];

  // The checkpoints...
  CheckpointManager   checkpoints;

#if JOIN == SPIN
  char padding2[128];

  // Workers from the last invocation
  // will set these flags when they die.
  // Faster than calling waitpid()...
  Bool workerDoneFlags[ MAX_WORKERS ];
#endif

};
typedef struct s_parallel_control_block ParallelControlBlock;

ParallelControlBlock *__specpriv_get_pcb(void);
void __specpriv_destroy_pcb(void);


#endif

