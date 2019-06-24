#ifndef LIBERTY_SPECPRIV_EXECUTIVE_CONFIG_H
#define LIBERTY_SPECPRIV_EXECUTIVE_CONFIG_H

#include "constants.h"

// Enable time measurements for performance tuning? 0 (off) or 1 (on)
#define TIMER             (0)

// If timing is enabled, what statistics/format to print
#define TIMER_PRINT_TIMELINE  (1)
#define TIMER_PRINT_OVERHEAD  (1)

// Enable debug messages? 0 (off) or 1 (on)
#define DEBUGGING         (0)
#define DEBUG_ON          DEBUGGING

// Print debug messages on misspeculation? 0 (off) or 1 (on)
#define DEBUG_MISSPEC     (1)

// Simulate misspeculation?
#define SIMULATE_MISSPEC  (0)

// Affinity policy: see constants.h
#define AFFINITY          ( SLEEP0 | RRPUNT0 | FIX | MP0STARTUP )

// Reduction method: VECTOR or NATIVE.
// NATIVE may be vectorized at the whim of your compiler.
#define REDUCTION         VECTOR

// Private memory method: VECTOR or NATIVE
#define SHADOW_MEM        NATIVE

// How do I join my workers? WAITPID or SPIN
#define JOIN              SPIN



// Number of processors; used to schedule affinities.
#define NUM_PROCS         (28)

// For deferred IO, assume that written string is shorter
// than this.  If wrong, we will need to try again.
#define BUFFER_SIZE       (128)

// A fixed (but arbitrary) limit on the number of
// workers for a single parallel invocation.
#define MAX_WORKERS       (64)

// How large should we allocate our heaps.
// Doesn't really matter; OS will only reserve
// space for the used portion.
#define HEAP_SIZE         (1*GB)

// Maximum number of bytes of checkpoint state
// that we will allocate at any time.
#define MAX_CHECKPOINT    (3UL*GB)

// Who tries to combine checkpoints?

// If SLOWEST_WORKER, this means that a worker will
// try to combine checkpoints if it was the last
// to commit into a checkpoint.  Simple, but penalizes
// the slowest worker by giving him more work.

// If FASTEST_WORKER, this means that a worker will
// try to combine checkpoints before attempting to
// allocate a new checkpoint object.  Tries to
// distribute checkpoint combination costs among
// workers who are making progress.

// These two options are NOT mutually
// exclusive.

// In either case, we will ALWAYS try to combine
// checkpoints if (1) the checkpoint manager has
// saturated, and (2) at worker-join.
#define WHO_DOES_CHECKPOINTS  (SLOWEST_WORKER)

#if DEBUGGING != 0
#define DEBUG(...)        do { __VA_ARGS__ ; } while(0)
#else
#define DEBUG(...)        do { } while(0)
#endif



#endif

