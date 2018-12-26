#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "config.h"
#include "event.h"

void free_evt(Event *evt)
{
  if( !evt )
    return;

#if DEBUG_LEVEL(2)
  fprintf(stderr, "Free event %ld\n", (uint64_t)evt);
#endif

  if( evt->time )
    free_tv( evt->time );

  evt->op = N_SOP_TYPES; // out-of-bounds

  free( evt );
}

void free_sop(SusOp *sop)
{
#if DEBUG_LEVEL(1)
  TX *parent = sop->base.parent;
  if( parent )
    assert( !queue_contains( &parent->queue, &sop->base ) );
#endif

  if( sop->buffer )
    free( sop->buffer );
  free_evt( &sop->base );
}

void run_sop(SusOp *sop)
{
  float *a,*b;
  unsigned i;

  Scalar rval = {0};
  switch( sop->base.op )
  {
    case FWRITE:
      for(rval.u32=0; rval.u32 < sop->arg1.u32; )
        rval.u32 += fwrite(&sop->buffer[ rval.u32 ], sizeof(char), sop->arg1.u32 - rval.u32, sop->file.fp);
      break;
    case FREAD:
      rval.i32 = fread(sop->arg1.vptr, sop->arg2.i32, sop->arg3.i32, sop->file.fp);
      break;
    case FFLUSH:
      rval.i32  = fflush(sop->file.fp);
      break;
    case FOPEN:
      rval.fp = fopen(sop->buffer, sop->arg1.cptr);
      break;
    case FCLOSE:
      rval.i32 = fclose(sop->file.fp);
      break;
    case FSEEK:
      rval.i32 = fseek(sop->file.fp, sop->arg1.i32, sop->arg2.i32);
      break;
    case FGETS:
      rval.vptr = (void*) fgets((char*)sop->arg1.vptr, sop->arg2.i32, sop->file.fp);
      break;
    case FERROR:
      rval.i32 = ferror( sop->file.fp );
      break;
    case WRITE:
      for(rval.u32=0; rval.u32 < sop->arg1.u32; )
        rval.u32 += write(sop->file.u32, &sop->buffer[ rval.u32 ], sop->arg1.u32 - rval.u32);
      break;
    case READ:
      rval.i32 = read(sop->file.i32, sop->arg1.vptr, sop->arg2.i32);
      break;
    case OPEN:
      rval.i32 = open(sop->buffer, sop->arg1.i32);
      break;
    case CLOSE:
      rval.i32 = close(sop->file.u32);
      break;
    case LSEEK:
      rval.i32 = lseek(sop->file.u32, sop->arg1.i32, sop->arg2.i32);
      break;
    case FXSTAT:
      rval.i32 = __fxstat(sop->arg1.i32, sop->file.i32,
        (struct stat*) sop->arg2.vptr);
      break;
    case XSTAT:
      rval.i32 = __xstat(sop->arg1.i32, sop->buffer,
        (struct stat*) sop->arg2.vptr);
      break;
    case EXIT:
      exit( sop->arg1.i32 );
      break;
    case PERROR:
      perror( sop->buffer );
      break;
    case REMOVE:
      rval.i32 = remove( sop->buffer );
      break;
    case MEM_LOAD:
      if( sop->arg1.i32 == 32 ) // 32-bit int/unsigned/float
        rval.u32 = *sop->arg2.u32ptr;

      else if( sop->arg1.i32 == 64 ) // 64-bit int/unsigned/double
        rval.u64 = *sop->arg2.u64ptr;

      else
        assert(0 && "Unsupported bit-width");
      break;

    case MEM_STORE:
      if( sop->arg1.i32 == 32 ) // 32-bit int/unsigned/float
        *sop->arg2.u32ptr = sop->arg3.u32;

      else if( sop->arg1.i32 == 64 ) // 64-bit int/unsigned/double
        *sop->arg2.u64ptr = sop->arg3.u64;

      else
        assert(0 && "Unsupported bit-width");
      break;

    case MEM_ADD:
      if( sop->arg1.i32 == 32 ) // 32-bit int/unsigned
        *sop->arg2.u32ptr += sop->arg3.u32;

      else if( sop->arg1.i32 == 64 ) // 64-bit int/unsigned
        *sop->arg2.u64ptr += sop->arg3.u64;

      else if( sop->arg1.i32 == -32 ) // 32-bit float
        *sop->arg2.fptr += sop->arg3.f;

      else if( sop->arg1.i32 == -64 ) // 64-bit double
        *sop->arg2.dptr += sop->arg3.d;

      else
        assert(0 && "Unsupported bit-width");
      break;

    case FADD_VEC:
      a = sop->arg1.fptr;
      b = sop->arg2.fptr;
      for(i=0; i<sop->arg3.u32; ++i)
        a[i] += b[i];
      break;

    case CALL:
      sop->arg1.fcn( sop->arg2.vptr );
      break;

    default:
      assert(0 && "bad suspended operation");
      break;
  }

  Result *result = sop->base.result;
  if( result )
    result->retval = rval;
}

void free_tx(TX *tx)
{
  if( !tx )
    return;

#if DEBUG == -1
  if( tx->already > 1 )
  {
    fprintf(stderr, "Free tx 0x%lx with already=%d ",
      (uint64_t)tx, tx->already);
    print_tx(tx);
    fprintf(stderr, "\n");
  }
#endif

  assert( size_q( &tx->queue ) == 0 );

  free_tv(tx->upto);
  destruct_q( &tx->queue );
  free_evt( (Event*)tx );
}

static const char *event_names[] =
  {
   "fwrite",
   "fread",
   "fflush",
   "fopen",
   "fclose",
   "fseek",
   "fgets",
   "ferror",
   "write",
   "read",
   "open",
   "close",
   "lseek",
   "__fxstat",
   "__xstat",
   "exit",
   "perror",
   "remove",
   "ld",
   "st",
   "add",
   "fadd-vec",
   "call",
   "sub-tx",
   "LAST EVENT"
  };

void print_evt(Event *evt)
{
  if( !evt )
  {
    fprintf(stderr, "null-evt");
    return;
  }

  print_time_rec( evt->parent );
  print_time( evt->time );

  fprintf(stderr, " %s", event_names[ evt->op ] );

}

void print_sop(SusOp *sop)
{
  int i;
  print_evt( &sop->base );

  if( sop->buffer != 0 )
  {
    fprintf(stderr, " \"");
    for(i=0; sop->buffer[i]; ++i)
      if( isprint( sop->buffer[i] ) )
        fprintf(stderr, "%c", sop->buffer[i]);
      else
        fprintf(stderr, "\\%o", sop->buffer[i]);
    fprintf(stderr, "\"");
  }
}

void print_time_rec(TX *tx)
{
  if( !tx )
    return;

  if( tx->base.parent )
    print_time_rec( tx->base.parent );

  print_time( tx->base.time );
}


void print_tx(TX *tx)
{
  print_evt( &tx->base );
  fprintf(stderr, "(");
  if( tx->ready )
    fprintf(stderr, "ready, ");
  if( tx->in_parent_q )
    fprintf(stderr, "in-parent-q, ");
  fprintf(stderr, "total %d, already %d, upto ",
    tx->total, tx->already);
  print_time( tx->upto );
#if MAX_LISTED_FDS > 0
  if( tx->restricted_fds[0] != ALLFILES )
  {
    fprintf(stderr, ", restricted_fd {");
    for(unsigned i=0; i<MAX_LISTED_FDS; ++i)
    {
      if( tx->restricted_fds[i] == ALLFILES )
      {
        fprintf(stderr, " all-files ");
        break;
      }
      else if( tx->restricted_fds[i] == EMPTY )
        continue;
      else
        fprintf(stderr, " %d ", tx->restricted_fds[i]);
    }
    fprintf(stderr, "}");
  }
#endif

#if DEBUG_LEVEL(1)
  if( tx->dbgname )
    fprintf(stderr, ", name `%s'", tx->dbgname);
#endif
  fprintf(stderr, ", queue %d)",
    size_q( &tx->queue ) );
}



