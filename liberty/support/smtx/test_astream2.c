/* This test checks the instrumentation
 * in sw_queue_astream.
 * should be compiled with -DINSTRUMENT
 */

#include <stdio.h>
#include <assert.h>

#include "sw_queue_astream.h"

#define N   (1000000)
#define M   (N/10)

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;

  int i,j;

  SW_Queue q = sq_createQueue();

  for(i=0; i<N; i+=M) {
   for(j=0; j<M; ++j) {
    uint64_t v = 0;
    sq_produce(q,v);
   }

   sq_flushQueue(q);

   for(j=0; j<M; ++j) {
    assert( sq_consume(q) == 0 );
   }
  }

  fprintf(stderr, "The next two numbers should be %d\n", N);
  sq_freeQueue(q);
  return 0;
}
