#ifndef SLAMPLIB_HOOKS_SLAMP_TIMESTAMP
#define SLAMPLIB_HOOKS_SLAMP_TIMESTAMP

#include <stdint.h>

typedef uint64_t TS; // first 20 bits for instr and following 44 bits for iter
#define TIMESTAMP_SIZE_IN_BYTES 8
#define TIMESTAMP_SIZE_IN_POWER_OF_TWO 3
#define ITERATION_SIZE 40
#define INVOCATION_SIZE 4 // 44-40
#define CREATE_TS(instr, iter, invoc) ( ((TS)instr << 44) | (((TS)iter & (TS)0xffffffffff) << INVOCATION_SIZE) | ((TS)invoc & (TS)0xf))
#define GET_INSTR(ts) ( (ts >> 44) & 0xfffff )
#define GET_ITER(ts) ( (ts >> INVOCATION_SIZE) & 0xffffffffff)
#define GET_INVOC(ts) ( ts & 0xf)


#endif
