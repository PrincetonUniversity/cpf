
#ifndef LIBERTY_PUREIO_EVENT_H
#define LIBERTY_PUREIO_EVENT_H

#include <stdio.h>

#include "config.h"
#include "types.h"
#include "tv.h"
#include "q.h"
#include "prio.h"

// The thunderdome!
union u_scalar
{
  int32_t       i32;
  uint32_t      u32;
  uint64_t      u64;
  float         f;
  double        d;
  void *        vptr;
  const char *  cptr;
  uint32_t   *  u32ptr;
  uint64_t   *  u64ptr;
  float *       fptr;
  double *      dptr;
  FILE *        fp;
  void        (*fcn)(void *);
};

//------------------------------------------------------------------------
// The Result type
// Holds a storage location for the result of an operation.
// Optionally, may hold a semaphore to indicate that the
// event has completed.

struct s_result
{
  // Someone may block on this transaction
  // finishing.
  sem_t *               sync;

  // The return value of the operation.
  Scalar                retval;
};

//------------------------------------------------------------------------
// The Event type
// Is the 'parent class' to SusOp and SubTx

enum Operation
{
  // If you update this, don't forget to update event names in event.c
  FWRITE=0,
  FREAD,
  FFLUSH,
  FOPEN,
  FCLOSE, //5
  FSEEK,
  FGETS,
  FERROR,
  WRITE,
  READ,   //10
  OPEN,
  CLOSE,
  LSEEK,
  FXSTAT,
  XSTAT,  //15
  EXIT,
  PERROR,
  REMOVE,
  MEM_LOAD,
  MEM_STORE,   // 20
  MEM_ADD,
  FADD_VEC,
  CALL,
  SUB_TX,
  N_SOP_TYPES // 25
};

struct s_event
{
  enum Operation op;

  Time    * time;
  TX      * parent;

  // Optional.
  // If present, the result(s) from the operation
  // will be stored into this structure.
  Result  * result;
};


void free_evt(Event *evt);

//------------------------------------------------------------------------
// Methods for manipulating SusOps.
// As the name implies, a SusOps is an object which
// represents a program side effect and all of its parameters.
//
// A SusOp is a 'subclass' of event.

struct s_suspended_operation
{
  Event   base;

  Scalar  file;

  // If this is not null,
  // free_sop() will free it.
  char  * buffer;

  Scalar  arg1;
  Scalar  arg2;
  Scalar  arg3;
};

void free_sop(SusOp *sop);
void run_sop(SusOp *sop);

//------------------------------------------------------------------------
// Methods for manipulating transactions
//
// A transaction is a 'subclass' of event
// which may contain other events.

#define UNKNOWN       (~0U)
#define EMPTY         (-3)
#define ALLFILES      (-2)
#define NOFILE        (-1)
struct s_transaction
{
  Event                 base;

#if DEBUG_LEVEL(1)
  // A name assigned to the this
  // transaction by the compiler
  const char *          dbgname;
#endif

  // Signifies that all events before
  // this TX have been committed, and
  // that events within this TX may
  // commit when they are ready.
  unsigned              ready:1;

  // Signifies that this TX has been
  // inserted into its parent's priority
  // queue already.  Only meaningul
  // when parent!=0
  unsigned              in_parent_q:1;

  // If this transaction is restricted to
  // a single file, record that file descriptor here.
  // If this tx is unrestricted, this will
  // contain ALLFILES.  If this tx does
  // not touch any files, it contains
  // NOFILE.  Unused entries will contain
  // EMPTY.
#if MAX_LISTED_FDS > 0
  int                   restricted_fds[ MAX_LISTED_FDS ];
#endif

#if USE_COMMIT_THREAD
  // A thread-to-thread queue.
  // There is one per root-tx; we copy it
  // to sub-tx for easy and quick lookup.
  Queue *               dispatch;
#endif

  // Number of sub-transactions, or UNKNOWN
  uint32_t              total;
  uint32_t              not_total;

  // Number of sub-transactions we have committed so far.
  uint32_t              already;

  // All sub-transactions we have committed so far
  // are <= this, or NULL if we haven't committed any yet.
  Time *                upto;

  // Priority queue of sub-txs
  PrioQueue             queue;
};

void free_tx(TX *tx);

void print_evt(Event *evt);
void print_sop(SusOp *sop);
void print_tx(TX *tx);
void print_time_rec(TX *tx);


#endif

