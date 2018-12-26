/* nospec-pipeline runtime
*/
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include "runtime.h"

static char numAvailableWorkers = 1;
static pthread_t *tids; // Store the tids of the workers
static int numWorkers;
static pthread_key_t __key;

/** Safe malloc so that we don't have to check return values all over.
 */
static void *smalloc(size_t size)
{
  void *ret = malloc(size);
  if(ret == NULL)
  {
    fprintf(stderr, "Malloc failed\n");
    exit(EXIT_FAILURE);
  }
  return ret;
}

/** Return num available workers, for now this is just a proxy for
 * inside/outside parallel region.
 */
Wid __specpriv_num_available_workers(void)
{
  return numAvailableWorkers;
}

/** Begin parallel invocation and mark state as inside parallel region.
 */
uint32_t __specpriv_begin_invocation(void)
{
  pthread_key_create(&__key, NULL);
  numAvailableWorkers = 0;
  return 0;
}

/** End the parallel invocation and mark state as outside parallel region.
 */
uint32_t __specpriv_end_invocation(void)
{
  pthread_key_delete(__key);
  numAvailableWorkers = 1;
  return 0;
}

typedef struct __specpriv_launch_t
{
  Iteration iterNum;
  vfnptrv startFun;
  void *arg;
} __specpriv_launch;

/** Springboard to setup the thread local stuff before launching the
 * actual parallel function.
 */
void *__specpriv_thread_launch(void *arg)
{
  __specpriv_launch *spl = (__specpriv_launch*)arg;
  void *ptr = NULL;
  Iteration *iNum = (Iteration *)smalloc(sizeof(Iteration));
  *iNum = spl->iterNum;
  pthread_setspecific(__key, iNum);
  ptr = spl->startFun(spl->arg);
  free(spl);
  return ptr;
}

/** Spawn workers for non-speculative execution
 * @param iterationNumber The iteration number to start execution on
 * @param startFun A pointer to the function to begin execution in
 * @param arg A pointer to the argument structure
 * @param stageNum The stage number
 */
uint32_t __specpriv_spawn_workers(Iteration iterationNumber,
    vfnptrv startFun, void* arg, int stageNum) {

  __specpriv_launch *spl;

  numWorkers = 2;
  const char *nw = getenv("NUM_WORKERS");
  if( nw )
  {
    int n = atoi(nw);
    assert( 1 <= n && n <= MAX_WORKERS );
    numWorkers = n;
  }

  tids = (pthread_t *)smalloc( sizeof(pthread_t) * numWorkers);
  spl = (__specpriv_launch*)smalloc( sizeof(__specpriv_launch) );
  spl->iterNum = iterationNumber;
  spl->startFun = startFun;
  spl->arg = arg;

  for(int i = 0; i < numWorkers; ++i)
  {
    pthread_create(&tids[i], NULL, __specpriv_thread_launch, (void*)spl);
  }

  return 0;
}

/** Join the children
 */
uint32_t __specpriv_join_children(void)
{
  for(int i = 0; i < numWorkers; ++i)
  {
    pthread_join(tids[i], NULL);
  }

  free(tids);

  return 0;
}

/** Begin a new iteration
 */
void __specpriv_begin_iter(void)
{

}

/** End the current iteration
 */
void __specpriv_end_iter(void)
{
  Iteration *iter = (Iteration *)pthread_getspecific(__key);
  (*iter)++;
}

/** Return the current iteration that we are on.
 */
Iteration __specpriv_current_iter(void)
{
  Iteration i = *(Iteration *)pthread_getspecific(__key);
  return i;
}

/** Called when a worker finishes execution
 */
uint32_t __specpriv_worker_finishes(Exit exitNumber)
{
  free(pthread_getspecific(__key));
  pthread_exit(NULL);
}

/** Create a queue
 */
struct __specpriv_queue * __specpriv_create_queue(void);

/** Free the queue
 */
void __specpriv_free_queue(struct __specpriv_queue *q);

/** Produce a value onto the specified queue
 */
void __specpriv_produce(struct __specpriv_queue *q, uint64_t v)
{

  // store v into the queue

}

/** Consume a value from the specified queue
 */
uint64_t __specpriv_consume(struct __specpriv_queue *q)
{
  uint64_t val = 0;

  // get the value from the queue here

  return val;
}



/* These are not used in the non-speculative runtime */
void __specpriv_reset_queue(struct __specpriv_queue *q) { }
Iteration __specpriv_misspec_iter(void) { return 0; }
Iteration __specpriv_last_committed(void) { return 0; }
void __specpriv_recovery_finished(Exit exitNumber) { }



/** Little test program to test out the various API calls
 */
#if 0

void *foo(void *arg)
{
  long int max = *(long int *)arg;
  long int i;
  Iteration iter;

  for(i=0; i<max; ++i)
  {
    __specpriv_begin_iter();
    printf("In foo (%x), iter %ld\n", pthread_self(), i);
    iter = __specpriv_current_iter();
    printf("In foo (%x), runtime current iter %d\n", pthread_self(), iter);
    __specpriv_end_iter();
  }

  __specpriv_worker_finishes(0);
  //pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
  printf("before invocation num_workers: %ld\n",__specpriv_num_available_workers());
  __specpriv_begin_invocation();
  printf("after invocation num_workers: %ld\n",__specpriv_num_available_workers());
  __specpriv_spawn_workers(0, foo, &argc, 0);
  __specpriv_join_children();
  __specpriv_end_invocation();
  printf("at end num_workers: %ld\n",__specpriv_num_available_workers());

  return 0;
}



#endif
