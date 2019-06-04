#ifndef LLVM_LIBERTY_SPEC_PRIV_CHECKPOINT_H
#define LLVM_LIBERTY_SPEC_PRIV_CHECKPOINT_H

#include "config.h"
#include "constants.h"
#include "types.h"
#include "heap.h"
#include "io.h"

// Life-cycle of a checkpoint
enum e_checkpoint_life
{
  CL_Free = 0,
  CL_Partial,
  CL_Complete,
  CL_Broken,
  CL_Main
};
typedef enum e_checkpoint_life CheckpointLife;

// A checkpoint is a version of the
// private/shared and redux heaps at
// some version in time.
typedef struct s_checkpoint Checkpoint;
struct s_checkpoint
{
  // Each checkpoint has it's own lock,
  // which you must acquire before
  // reading/writing any fields.
  // Exception: the prev/next pointers
  // are guarded by the list's lock.
  unsigned        lock;

  CheckpointLife  type;

  Heap            heap_priv;
  Heap            heap_shadow;
  Heap            heap_redux;

  // A range [lo,hi) of bytes from the private
  // heap which were touched by the parallel
  // region.  These addresses are relative to
  // the natural address of the shadow heap.
  uint8_t *       shadow_lowest_inclusive,
          *       shadow_highest_exclusive;

  Len             redux_used;

  // iteration of last update for min/max reduction that depends on another
  // reduction. Found in KS benchmark
  Iteration       lastUpdateIteration;

  Iteration       iteration;
  unsigned        num_workers;

  IOEvtSet        io_events;

  // List structure: guarded by
  // the list's lock.
  Checkpoint      *prev, *next;
};

// A list of partial checkpoints.
typedef struct s_checkpoint_list CheckpointList;
struct s_checkpoint_list
{
  Checkpoint      *first, *last;
};

// A checkpoint manager maintains a list of
// partial checkpoints, in order by time.
// It also holds a set of free()d checkpoints
// to save allocation time.  The list contains
// a lock, which must be acquired before changing
// list structure (which members in which order).
typedef struct s_checkpoint_manager CheckpointManager;
struct s_checkpoint_manager
{
  unsigned        lock;
  unsigned        total_checkpoint_objects;

  // There are many checkpoints.
  // At any given time, this is the version
  // mapped by the main process.
  Checkpoint *    main_checkpoint;

  CheckpointList  used, free;
};

void __specpriv_destroy_checkpoint(Checkpoint *chkpt);

void __specpriv_init_checkpoint_list(CheckpointList *list);
void __specpriv_destroy_checkpoint_list(CheckpointList *list);

void __specpriv_init_checkpoint_manager(CheckpointManager *mgr);
void __specpriv_destroy_checkpoint_manager(CheckpointManager *mgr);

Checkpoint *__specpriv_alloc_checkpoint(CheckpointManager *mgr);
void __specpriv_free_checkpoint(CheckpointManager *mgr, Checkpoint *chkpt);

Checkpoint *__specpriv_get_checkpoint_for_iter(CheckpointManager *mgr, Iteration i);

// Called by each worker when he is ready to perform a checkpoint
void __specpriv_worker_perform_checkpoint(int isFinalCheckpoint);

void __specpriv_distill_checkpoints_into_liveout(CheckpointManager *mgr);

Bool __specpriv_commit_zero_or_more_checkpoints(CheckpointManager *mgr);

#endif

