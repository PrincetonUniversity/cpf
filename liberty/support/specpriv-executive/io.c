#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#include "config.h"
#include "api.h"
#include "timer.h"
#include "io.h"
#include "pcb.h"
#include "heap.h"
#include "fiveheaps.h"

Be a bad guy!

// Deferred IO

static IOEvt *worker_io_events = 0;
static unsigned worker_cap_io_events;
static unsigned worker_num_io_events;

void __specpriv_reset_worker_io(void)
{
  worker_cap_io_events = 0;
  worker_num_io_events = 0;
  worker_io_events = 0;
}

void __specpriv_copy_io_to_redux(IOEvtSet *evtset, MappedHeap *redux)
{
  uint64_t start;
  TIME(start);

  const Wid myWorkerId = __specpriv_my_worker_id();

  evtset->num[ myWorkerId ] = worker_num_io_events;
  if( worker_num_io_events < 1 || worker_io_events == 0 )
  {
    __specpriv_reset_worker_io();
    return;
  }

  // The worker produced IO.
  // Copy my IO into the high-half of the
  // checkpoint's reduction heap (after all of the reductions)

  // First, copy the strings to the redux heap.
  for(unsigned i=0; i<worker_num_io_events; ++i)
  {
    void *old_buffer = worker_io_events[i].buffer;
    unsigned len = worker_io_events[i].len;
    void *new_buffer = heap_alloc(redux,len+1);

    memcpy(new_buffer, old_buffer, len);

    worker_io_events[i].buffer = heap_inv_translate(new_buffer,redux);
    free(old_buffer);
  }

  // Now copy my list of events.
  // IMPORTANT NOTE: we are copying pointers
  const unsigned list_size = worker_num_io_events * sizeof(IOEvt);
  IOEvt *new_list = (IOEvt*) heap_alloc(redux, list_size);
  memcpy(new_list, worker_io_events, list_size);
  free( worker_io_events );

  // And report it in the global parallel control block.
  evtset->lists[ myWorkerId ] = (IOEvt*) heap_inv_translate(new_list,redux);

  // Free up my IO queue.
  // Leave my list at its current capacity though.
  __specpriv_reset_worker_io();

  TADD(worker_copy_io_to_redux_time, start);
}

void __specpriv_commit_io(IOEvtSet *evtset, MappedHeap *redux)
{
  uint64_t start;
  TIME(start);

  const Wid numWorkers = __specpriv_num_workers();

  // IMPORTANT NOTE:
  // The IO lists contain pointers to objects within the redux heaps
  // of each worker.  Since we are remapping the worker heaps to
  // different locations, we MUST correct the pointers.

  // Perform the deferred IO operations from each worker.
  unsigned offsets[ MAX_WORKERS ] = {0};
  for(;;)
  {
    // Find the next IO event to issue.
    Iteration minTime = 0;
    unsigned minTimeIndex = UINT32_MAX;
    for(Wid wid=0; wid<numWorkers; ++wid)
    {
      const unsigned ow = offsets[wid];

      if( ow >= evtset->num[wid] )
        continue;

      // CORRECT POINTER
      IOEvt *bad_iolist = evtset->lists[wid];
      IOEvt *corrected_iolist = (IOEvt*) heap_translate( bad_iolist, redux );

      const Iteration evtTime = corrected_iolist[ ow ].iter;

      if( evtTime < minTime
      ||  minTimeIndex == UINT32_MAX )
      {
        minTimeIndex = wid;
        minTime = evtTime;
      }
    }

    // No more IO left?
    if( minTimeIndex == UINT32_MAX )
      break;

    // Perform that IO operation.

    // CORRECT POINTER
    IOEvt *bad_iolist = evtset->lists[ minTimeIndex ];
    IOEvt *corrected_iolist = (IOEvt*) heap_translate( bad_iolist, redux );

    unsigned ow = offsets[ minTimeIndex ];

    for(const Iteration iter = corrected_iolist[ ow ].iter; corrected_iolist[ ow ].iter == iter; ++ow)
    {
      IOEvt *evt = & corrected_iolist[ ow ];

      // CORRECT POINTER
      void *bad_buffer = evt->buffer;
      void *corrected_buffer = heap_translate(bad_buffer, redux );

//      printf("Process io event #%u from worker %u at time %d\n", ow, minTimeIndex, evt->iter);

      // Perform the operation.
      FILE *file = evt->stream;
      const unsigned len = evt->len;
      int result = fwrite(corrected_buffer, 1, len, file);
      fflush(file);
      heap_free(redux, corrected_buffer);

      assert( result >= 0 && ((unsigned)result) == len && "Can't fix this");
    }

    offsets[ minTimeIndex ] = ow;
  }

  // Free empty the set.
  for(Wid wid=0; wid<numWorkers; ++wid)
  {
    evtset->num[wid] = 0;

    void *bad_list = evtset->lists[wid];
    void *corrected_list = heap_translate(bad_list, redux);
    heap_free(redux, corrected_list);

    evtset->lists[wid] = 0;
  }

  TADD(worker_commit_io_time, start);
}



// -----------------------------------------------------------------------
// Deferred IO operations

static IOEvt *__specpriv_grow_io(void)
{
  if( (worker_num_io_events + 1) > worker_cap_io_events )
  {
    worker_cap_io_events *= 2;
    if( 64 > worker_cap_io_events )
      worker_cap_io_events = 64;

    worker_io_events = (IOEvt*) realloc(worker_io_events, worker_cap_io_events*sizeof(IOEvt) );
  }

  return &worker_io_events[ worker_num_io_events++ ];
}

static void __specpriv_issue_io(void *buffer, size_t len, FILE *file)
{
  IOEvt *evt = __specpriv_grow_io();

  evt->iter = __specpriv_current_iter();
  evt->stream = file;
  evt->len = len;
  evt->buffer = buffer;
}


int __specpriv_io_fwrite(void *buffer, size_t size, size_t nmemb, FILE *file)
{
  TOUT(
      __specpriv_add_right_time( &worker_on_iteration_time, &worker_off_iteration_time,
        worker_pause_time);
      );
  uint64_t start;
  TIME(start);

  const size_t len = size * nmemb;
  void *buff = malloc( len );
  memcpy(buff, buffer, len);
  __specpriv_issue_io(buff, len, file);

  TADD(worker_intermediate_io_time,start);
  TIME(worker_pause_time);
  return nmemb;
}

int __specpriv_io_vfprintf(FILE *file, const char *fmt, va_list ap)
{
  TOUT(
      __specpriv_add_right_time( &worker_on_iteration_time, &worker_off_iteration_time,
        worker_pause_time);
      );
  uint64_t start;
  TIME(start);

  char *buffer = (char*) malloc( BUFFER_SIZE * sizeof(char) );
  int len = vsnprintf(buffer, BUFFER_SIZE, fmt, ap);
  if( len >= BUFFER_SIZE )
  {
    buffer = (char*) realloc( buffer,  (1+len) * sizeof(char) );
    len = vsnprintf(buffer, len+1, fmt, ap);
  }
  va_end(ap);

  if( len > 0 )
    __specpriv_issue_io(buffer, len, file);
  else
    free(buffer);

  TADD(worker_intermediate_io_time,start);
  TIME(worker_pause_time);
  return len;
}

int __specpriv_io_printf(const char *fmt, ...)
{
  /* TOUT( */
  /*     __specpriv_add_right_time( &worker_on_iteration_time, &worker_off_iteration_time, */
  /*       worker_pause_time); */
  /*     ); */
  /* uint64_t start; */
  /* TIME(start); */
  va_list ap;
  va_start(ap,fmt);

  int retval = __specpriv_io_vfprintf(stdout, fmt, ap);
  /* TADD(worker_intermediate_io_time, start); */
  /* TIME(worker_pause_time); */
  return retval;
}

int __specpriv_io_fprintf(FILE *file, const char *fmt, ...)
{
  /* TOUT( */
  /*     __specpriv_add_right_time( &worker_on_iteration_time, &worker_off_iteration_time, */
  /*       worker_pause_time); */
  /*     ); */
  /* uint64_t start; */
  /* TIME(start); */
  va_list ap;
  va_start(ap,fmt);

  int retval = __specpriv_io_vfprintf(file,fmt,ap);
  /* TADD(worker_intermediate_io_time, start); */
  /* TIME(worker_pause_time); */
  return retval;
}

// gc14 - timer for this will be less accurate since it calls __specpriv_io_printf()
int __specpriv_io_puts(const char *str)
{
  __specpriv_io_printf("%s\n",str);
  return 0;
}

// gc14 - timer for this will be less accurate since it calls __specpriv_io_printf()
int __specpriv_io_putchar(int c)
{
  __specpriv_io_printf("%c", c);
  return c;
}

int __specpriv_io_fflush(FILE *stream)
{
  TOUT(
      __specpriv_add_right_time( &worker_on_iteration_time, &worker_off_iteration_time,
        worker_pause_time);
      );
  TIME(worker_pause_time);
  return 0;
}


