#ifndef LIBERTY_SPECPRIV_EXECUTIVE_CONSTANTS_H
#define LIBERTY_SPECPRIV_EXECUTIVE_CONSTANTS_H

#include "types.h"

// config choices for JOIN
#define WAITPID           (0)
#define SPIN              (1)

// config choices for REDUCTION
#define NAIVE             (0)
#define VECTOR            (1)

// Affinity policy constants.
// Should the worker immediately call sleep(0) ?
#define SLEEP0            (1<<0)

// Set affinity to punt a worker off of processor 0.
// RRPUNT = punt it to any processor except 0.
// RRPUNT0 = punt it to any processor including 0.
#define RRPUNT            (1<<1)
#define RRPUNT0           (1<<2)

// Relax affinity after punting?
// ALLBUT1 = relax to any processor except 0
// ANY = relax to any processor including 0
// FIX = do not relax affinity
#define ALLBUT1           (1<<3)
#define ANY               (1<<4)
#define FIX               (1<<5)

// Pin the main process to run on
// processor 0 at application startup
// or at parallel invocation
#define MP0STARTUP        (1<<6)
#define MP0INVOC          (1<<7)

// Affinity assignments should use
// a os_cpu_id -> physical_cpu_id map.
#define OS2PHYS           (1<<8)

// Options for WHO_DOES_CHECKPOINTS
#define FASTEST_WORKER    (1<<0)
#define SLOWEST_WORKER    (1<<1)

// Pointer coding
#define POINTER_BITS      (44)

#define POINTER_MASK      (7ULL << POINTER_BITS)

#define META_ADDR         (1ULL << POINTER_BITS)
#define REDUX_ADDR        (2ULL << POINTER_BITS)
#define PRIV_ADDR         (3ULL << POINTER_BITS)
#define SHARED_ADDR       (4ULL << POINTER_BITS)
#define RO_ADDR           (5ULL << POINTER_BITS)
#define LOCAL_ADDR        (6ULL << POINTER_BITS)
#define SHADOW_ADDR       (7ULL << POINTER_BITS)


// Sizes of kibi-, mibi-, and gibi-bytes
#define KB                (1024)
#define MB                (1024*KB)
#define GB                (1024*MB)

// A worker-id code to signify the main process
#define MAIN_PROCESS      (~(Wid)0)

// Round-up/-down to a power of two
#define ROUND_DOWN(n,k)   ( (~((k)-1)) & (uint64_t) (n) )
#define ROUND_UP(n,k)     ROUND_DOWN( (n) + ((k) - 1), (k))

// Align all allocation units to a multiple of this.
// MUST BE A POWER OF TWO
#define ALIGNMENT         (16)

// We collect shadow metadata at the per-byte granularity, and so
// each piece of metadata must fit in a byte:
#define NUM_DISTINCT_UINT8_VALUES   (1 << (8 * sizeof(uint8_t)))

// We have two reserved values:
//  0    : live in.
//  1    : defined in some iteration earlier than the most recent checkpoint.
//  2    : read during this iteration.
//  3--n : defined in an iteration later than the last checkpoint.
#define NUM_RESERVED_SHADOW_VALUES  (3)

// Range of iterations since last checkpoint.
#define MAX_CHECKPOINT_GRANULARITY  ( NUM_DISTINCT_UINT8_VALUES - NUM_RESERVED_SHADOW_VALUES )

// A shadow-memory code to signify live-in values
#define LIVE_IN           ( (uint8_t) 0 )

// A shadow-memory code to signify that a value was read, and that
// we cannot initially say it was a cross iteration flow.  I.e. given
// local information, it appears to be a read of a live-in value.
// We mark it as such, so that we can check again later as checkpoints
// are constructed/merged.
#define READ_LIVE_IN      ( (uint8_t) 1 )

// A shadow-memory code to signify that a value was defined within
// the loop by an iteration earlier than the most recent checkpoint.'
#define OLD_ITERATION     ( (uint8_t) 2 )


#define WAS_WRITTEN_EVER(b)        ( (b) >= OLD_ITERATION )
#define WAS_WRITTEN_RECENTLY(b)    ( (b) >= NUM_RESERVED_SHADOW_VALUES )

// 16-, 32-, and 64-bit vectors of a constant byte value
#define V16(b)            ( (((uint16_t)(b))<<8) | ((uint16_t)b) )
#define V32(b)            ( (((uint32_t)V16(b))<<16) | (uint32_t)V16(b) )
#define V64(b)            ( (((uint64_t)V32(b))<<32) | (uint64_t)V32(b) )

// If we are doing timing, how many checkpoint records should we
// keep per worker per invocation?
#define MAX_CHECKPOINTS   ( 64 )

// Last iteration
#define LAST_ITERATION    ( INT32_MAX )

// prefixing function names
#define CAT(a,b) a##b
#define PREFIX(x) CAT(__specpriv_, x)

#define NOT_A_PARALLEL_STAGE (~(Wid)0)

#endif
