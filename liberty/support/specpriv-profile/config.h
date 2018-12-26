#ifndef SPECPRIV_CONFIG_H
#define SPECPRIV_CONFIG_H

// The maximum distinct number of observations
// for integer value prediction.
#define MAX_INT_PREDICTION_OBSERVATIONS     (3U)


// The maximum distinct number of observations
// for pointer value prediction.
#define MAX_POINTER_PREDICTION_OBSERVATIONS (3U)


// The maximum distinct number of observations
// for underlying objects.
#define MAX_UNDERLYING_OBJECT_OBSERVATIONS  (5U)

// Turn on debugging messages?
#define DEBUG                               (false)

// If debugging is enabled, this flag will cause the
// profiler to abort after an unknown object is found.
#define STOP_ON_FIRST_UNKNOWN               (false)

#define TIMER                               (0)

#endif

