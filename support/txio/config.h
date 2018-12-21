#ifndef LIBERTY_PUREIO_CONFIG_H
#define LIBERTY_PUREIO_CONFIG_H

#define DEBUG (0)

#define DEBUG_LEVEL(n)  ( DEBUG >= n )

// Size of the inter-thread communication
// queue.
#define N_SLOTS         (32)

// Queuelets per queue, or 1
// to disable.
#define QLETS_PER_Q     (9)

// Default size of a string buffer
// for suspended operations
#define BUFFER_SIZE     (64)

// Minimum size of a non-empty
// priority queue.
#define MIN_CAPACITY    (16)

// Should events be processed
// in the same thread as the
// latest issue, or in a
// special commit thread?
#define USE_COMMIT_THREAD (1)

// Collect statistics?
#define STATISTICS        (0)

// Maximum number of FDs that
// can be listed for a TX, or
// zero to disable this feature.
#define MAX_LISTED_FDS    (0)

#endif

