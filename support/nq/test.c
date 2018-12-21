
int __empty_c_files_violate_the_standard;

#ifdef DO_NOT_DEFINE_THIS_MACRO

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <linux/unistd.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <syscall.h>
#include <sys/syscall.h>


#include "nq.h"

#define N         (1000000000L)

#define K         (1024)

int gettid(void) {
  // for some reason, my glibc doesn't export this
  return syscall(SYS_gettid);
}

double timespec2double(struct timespec *start) {
  return start->tv_sec + start->tv_nsec / 1.e+9;
}

void write_stats(const char *who, uint64_t sum, struct timespec *start, struct timespec *stop, size_t n) {

  double start_time = timespec2double(start);
  double stop_time  =  timespec2double(stop);

  double duration = stop_time - start_time;
  double time_per_step = duration / (double) n;

  printf("Stat: %s reports sum=%'lld in  %'lf sec / %'d transfers == %'1.10lf sec/transfer\n",
    who, sum, duration, n, time_per_step);
}

void *producer_worker(void *arg) {

  printf("Starting the producer worker (tid=%d)...\n", gettid());

  // put this on core zero
  unsigned long mask = 1<<0;
  sched_setaffinity( gettid(), sizeof(mask), &mask);

  Producer *prod = (Producer*) arg;

  uint64_t i;
  uint64_t sum;
  struct timespec start, stop;

  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
  // send N values
  for(i=1,sum=0; i<=N; i++) {
    nq_produce(prod, i);
    sum += i;
  }
  nq_produce(prod, (uint64_t)0 );
  // end the termination value
  nq_flush(prod);
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stop);

  write_stats("producer", sum, &start, &stop, N+1);

  return 0;
}

void *consumer_worker(void *arg) {

  printf("Starting the consumer worker (tid=%d)...\n", gettid());

  // put this on core one
  unsigned long mask = 1<<1;
  sched_setaffinity( gettid(), sizeof(mask), &mask);



  Consumer *cons = (Consumer*) arg;

  struct timespec start, stop;

  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
  // consume until we receive the termination value 0
  uint64_t sum=0, v;
  size_t n=0;
  do {
    v = nq_consume(cons);
    sum += v;
    n++;
  } while( v > 0 );
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stop);

  write_stats("consumer", sum, &start, &stop, n);

  return 0;
}

int main(void) {

  printf("Cachline size is assumed to be %d bytes.\n", CACHELINE_SIZE);
  printf("\t- uin64_t/Chunk = %d.\n", CHUNK_SIZE );
  printf("\t- sizeof(Chunk) = %d.\n",  sizeof(Chunk) );
  printf("\t- sizeof(Pathway) = %d.\n",  sizeof(Pathway) );
  printf("\t- sizeof(Producer) = %d.\n",  sizeof(Producer) );
  printf("\t- sizeof(Consumer) = %d.\n",  sizeof(Consumer) );

  size_t sz = sizeof(Producer) + sizeof(Consumer) + sizeof(Pathway) + 3*sizeof(Chunk);
  printf("\t- max memory consumption per queue is %d <= %d K.\n",
    sz, (sz + K - 1)/K);

  struct timespec resolution;
  clock_getres(CLOCK_THREAD_CPUTIME_ID, &resolution);
  printf("Clock resolution is reported to be %'1.9lf sec.\n",
    timespec2double(&resolution));

  printf("Performing queue test of %'ld transfers "
    "across two threads on different cores.\n\n",
    N);

  Consumer *cons = nq_new_consumer();
  Producer *prod = nq_new_producer(cons);

  pthread_t cworker, pworker;
  pthread_create(&cworker, 0, &consumer_worker, (void*) cons);
  pthread_create(&pworker, 0, &producer_worker, (void*) prod);

  // do stuff in child threads

  pthread_join(pworker, 0);
  pthread_join(cworker, 0);

  printf("\nTo interpret these numbers, please ensure that both threads "
         "report the same sum and the same number of transfers.\n");

  nq_delete_producer(prod);
  nq_delete_consumer(cons);
  return 0;
}
#endif

