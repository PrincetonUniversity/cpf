#ifndef LLVM_LIBERTY_SPEC_PRIV_EXECUTIVE_IO_H
#define LLVM_LIBERTY_SPEC_PRIV_EXECUTIVE_IO_H

#include <stdio.h>
#include "config.h"
#include "constants.h"
#include "types.h"
#include "heap.h"

// A single deferred IO operation
struct s_io_evt
{
  Iteration   iter;
  FILE *      stream;
  size_t      len;
  void *      buffer;
};
typedef struct s_io_evt IOEvt;

// A set of events, divided by
// workers and ordered by time, ascending.
struct s_io_evt_set
{
  IOEvt *     lists[ MAX_WORKERS ];
  unsigned    num[ MAX_WORKERS ];
};
typedef struct s_io_evt_set IOEvtSet;


void __specpriv_reset_worker_io(void);

void __specpriv_copy_io_to_redux(IOEvtSet *evtset, MappedHeap *redux);
void __specpriv_commit_io(IOEvtSet *evtset, MappedHeap *redux);


#endif

