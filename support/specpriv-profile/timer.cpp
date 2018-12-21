#include "timer.h"

uint64_t rdtsc(void)
{
  uint32_t a, d;
  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) <<32 );
}

