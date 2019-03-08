#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "inline.h"

typedef struct {
  uint64_t start;
  uint64_t stop;
  uint64_t elapsed;
} watch_t;

Inline watch_t * watch_create (int nTimer) {
  return (watch_t *) malloc (sizeof(watch_t) * nTimer);    
}

Inline void watch_destroy (watch_t * timer) {
  free (timer);
}

Inline void watch_start (watch_t * timer) {
  unsigned a, d; 
  asm volatile("rdtsc" : "=a" (a), "=d" (d));  
  timer->start = ((uint64_t)a) | (((uint64_t)d) << 32);
  timer->elapsed=0;
}

Inline void watch_stop (watch_t * timer) {
  unsigned a, d; 
  asm volatile("rdtsc" : "=a" (a), "=d" (d)); 
  timer->stop = (((uint64_t)a) | (((uint64_t)d) << 32)) ;
  timer->elapsed = timer->elapsed + (timer->stop - timer->start);
}

Inline void watch_pause (watch_t *timer) {
  unsigned a, d;
  asm volatile("rdtsc" : "=a" (a), "=d" (d));
  timer->stop = (((uint64_t)a) | (((uint64_t)d) <<32));
  timer->elapsed = timer->elapsed + (timer->stop-timer->start);
  timer->start = timer->stop;
}

Inline void watch_restart(watch_t *timer) {
  unsigned a, d;
  asm volatile("rdtsc" : "=a" (a), "=d" (d));
  timer->start = ((uint64_t)a) | (((uint64_t)d) <<32 );
}

Inline void wait_ticks(uint64_t ticks) {
  uint64_t current_time;
  uint64_t time;
  unsigned a, d;     
  asm volatile("rdtsc" : "=a" (a), "=d" (d)); 
  time = (((uint64_t)a) | (((uint64_t)d) << 32)) ;
  time += ticks;
  do {
    asm volatile("rdtsc" : "=a" (a), "=d" (d)); 
    current_time = (((uint64_t)a) | (((uint64_t)d) << 32)) ;
  } while (current_time < time);
}
