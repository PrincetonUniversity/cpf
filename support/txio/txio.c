
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <semaphore.h>
#include <sys/stat.h>

#include "txio.h"

#include "config.h"
#include "tv.h"
#include "prio.h"
#include "event.h"
#include "commit.h"


#if STATISTICS
static uint32_t num_allocated=0;
#endif



//------------------------------------------------------------------------
// Methods exposed to the client

EpochId __epoch(EpochId e, uint32_t n, ...)
{
#if STATISTICS
  ++num_allocated;
#endif

  assert( n < 16 && "Insanity (1)");

  va_list vaargs;
  uint32_t i;

  Event *p = (Event*) e;
  assert( p );
  assert( p->op == SUB_TX );
  TX *parent = (TX*)p;

  SusOp *sop = (SusOp*) malloc( sizeof(SusOp) );
  sop->base.op = N_SOP_TYPES; // unknown, but definitely not SUB_TX
  sop->base.parent = parent;
  sop->base.result = 0;

  // Initialize our time.
  va_start(vaargs, n);
  sop->base.time = (Time *) malloc( sizeof(uint32_t) + n * sizeof(uint32_t) );
  sop->base.time->length = n;
  for(i=0; i<n; ++i)
    sop->base.time->indices[i] = va_arg(vaargs, uint32_t);
  va_end(vaargs);

  return (EpochId) sop;
}

// This method is exposed to the client.
EpochId __root_epoch(void)
{
#if STATISTICS
  ++num_allocated;
#endif


  TX *tx = (TX*) malloc( sizeof(TX) );
  tx->base.op = SUB_TX;
  tx->base.parent = 0;
  tx->base.result = 0;

#if DEBUG_LEVEL(1)
  tx->dbgname = "root";
#endif

  tx->ready = 1;
#if MAX_LISTED_FDS > 0
  for(unsigned i=0; i<MAX_LISTED_FDS; ++i)
    tx->restricted_fds[i] = EMPTY;
#endif
  tx->in_parent_q = 1;
  tx->base.time = 0;

  tx->total = UNKNOWN;
  tx->not_total = UNKNOWN;
  tx->already = 0;
  tx->upto = 0;
  init_q( &tx->queue );

#if DEBUG_LEVEL(2)
  fprintf(stderr, "\t\t\t[[[ Root: ");
  print_tx(tx);
  fprintf(stderr, "]]]\n");
#endif

#if USE_COMMIT_THREAD
  // Start the commit thread
  tx->dispatch = gg_new_queue();
  pthread_t commit;
  pthread_create(&commit,0,&commit_thread,(void*)tx);
#endif

  return (EpochId) tx;
}



// This method is exposed to the client.
EpochId __open_subepoch(EpochId e, const char *dbgname, uint32_t n, ...)
{
#if STATISTICS
  ++num_allocated;
#endif

  assert( n < 16 && "Insanity (1)");

  va_list vaargs;
  uint32_t i;

  Event *p = (Event *) e;
  TX *parent = (TX*)p;
  assert( parent );
  assert( parent->base.op == SUB_TX );

  TX *tx = (TX*) malloc( sizeof(TX) );
  tx->base.op = SUB_TX;
  tx->base.parent = parent;
  tx->base.result = 0;

#if DEBUG_LEVEL(1)
  tx->dbgname = dbgname;
#endif

  // Initialize our time.
  va_start(vaargs, n);
  tx->base.time = (Time *) malloc( sizeof(uint32_t) + n * sizeof(uint32_t) );
  tx->base.time->length = n;
  for(i=0; i<n; ++i)
    tx->base.time->indices[i] = va_arg(vaargs, uint32_t);
  va_end(vaargs);


  tx->ready = 0;
#if MAX_LISTED_FDS > 0
  for(unsigned i=0; i<MAX_LISTED_FDS; ++i)
    tx->restricted_fds[i] = EMPTY;
#endif
  tx->in_parent_q = 0;

#if USE_COMMIT_THREAD
  tx->dispatch = parent->dispatch;
#endif
  tx->base.result = 0;

  tx->total = UNKNOWN;
  tx->not_total = UNKNOWN;
  tx->already = 0;
  tx->upto = 0;
  init_q( &tx->queue );

#if DEBUG_LEVEL(2)
  fprintf(stderr, "\t\t\t[[[ Open: ");
  print_tx(tx);
  fprintf(stderr, "]]]\n");
#endif

  return (EpochId) tx;
}



// Front-end functions called by the client.

// This method is exposed to the client
void __close_epoch(EpochId epoch, uint32_t n)
{
  Event *evt = (Event*)epoch;
  assert(evt->op == SUB_TX);

  TX *tx = (TX*)evt;
  close_tx(tx,n,0);
}


// This method is exposed to the client.
void __close_epoch_blocking(EpochId epoch, uint32_t n)
{
  Event *evt = (Event*)epoch;
  assert(evt->op == SUB_TX);

  TX *tx = (TX*)evt;
  close_tx(tx,n,1);

#if STATISTICS
  fprintf(stderr, "Num allocated %d\n", num_allocated);
#endif
}

EpochId __announce_restricted(EpochId e, ...)
{
#if MAX_LISTED_FDS > 0
  assert(e);
  TX *tx = (TX*)e;
  assert(tx->base.op == SUB_TX);

  va_list ap;
  va_start(ap, e);

  for(unsigned i=0;;++i)
  {
    uint32_t code = va_arg(ap, uint32_t);
    if( code == 0 )
      break;

    if( i >= MAX_LISTED_FDS )
      return e;

    else if( code == 1 ) // file descriptor int.
    {
      int fd = va_arg(ap,int);
      tx->restricted_fds[i] = fd;
    }

    else if( code == 2 ) // file pointer.
    {
      FILE *fp = va_arg(ap,FILE*);
      if( fp == 0 )
        continue;
      int fd = fileno(fp);
      tx->restricted_fds[i] = fd;
    }

    else if( code == 3 ) // file name string
    {
//      const char *name = va_arg(ap,const char*);
      assert(0 && "Not yet implemented.");
      // TODO
    }
  }

  va_end(ap);

  // Dispatch this TX
  dispatch_event(tx, &tx->base);

#endif
  return e;
}

int __txio_fwrite(EpochId e, void *ptr, size_t size, size_t nelt, FILE *file)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  const unsigned length = size*nelt;
  char *buffer = (char*) malloc(length);
  memcpy(buffer, ptr, length);

  // Construct a suspended operation object.
  sop->base.op = FWRITE;
  sop->base.result = 0;
  sop->file.fp = file;
  sop->buffer = buffer;
  sop->arg1.u32 = length;

  issue(sop,0);

  return nelt;
}

size_t __txio_write(EpochId e, int fd, void *ptr, size_t length)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  char *buffer = (char*) malloc(length);
  memcpy(buffer, ptr, length);

  // Construct a suspended operation object.
  sop->base.op = WRITE;
  sop->base.result = 0;
  sop->file.i32 = fd;
  sop->buffer = buffer;
  sop->arg1.u32 = length;

  issue(sop,0);

  return length;
}


int __txio_vfprintf(EpochId e, FILE *file, const char *fmt, va_list ap)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // Try to do the printf() into a fixed length
  // buffer
  char *buffer = (char*) malloc(BUFFER_SIZE);
  int length = vsnprintf(buffer, BUFFER_SIZE, fmt, ap);

  // If the buffer was too small, try a second
  // time with the right size buffer
  if( length + 1 > BUFFER_SIZE )
  {
    buffer = (char*)realloc(buffer, length+1);
    vsnprintf(buffer, length+1, fmt, ap);
  }

  va_end(ap);

  if( length < 1 )
  {
    free(buffer);
    return length;
  }

  // Construct a suspended operation object.
  sop->base.op = FWRITE;
  sop->base.result = 0;
  sop->file.fp = file;
  sop->buffer = buffer;
  sop->arg1.u32 = length;

  issue(sop,0);

  return length;
}

int __txio_vprintf(EpochId e, const char *fmt, va_list ap)
{
  return __txio_vfprintf(e,stdout,fmt,ap);
}

int __txio_fprintf(EpochId e, FILE *file, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  int r = __txio_vfprintf(e,file,fmt,ap);

  va_end(ap);
  return r;
}

int __txio_printf(EpochId e, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  int r = __txio_vfprintf(e,stdout,fmt,ap);

  va_end(ap);
  return r;
}

int __txio_fputs(EpochId e, FILE *file, const char *fmt)
{
  return __txio_fprintf(e, file, "%s\n", fmt);
}

int __txio_puts(EpochId e, const char *fmt)
{
  return __txio_fputs(e,stdout,fmt);
}

int __txio_fputc(EpochId e, int c, FILE *file)
{
  __txio_fprintf(e, file, "%c", (char)c);

  return (int)(unsigned char)c;
}

int __txio_putc(EpochId e, int c, FILE *file)
{
  return __txio_fputc(e,c,file);
}

int __txio_putchar(EpochId e, int c)
{
  return __txio_fputc(e,c,stdout);
}

int __txio__IO_putc(EpochId e, int c, FILE *file)
{
  return __txio_fputc(e,c,file);
}

int __txio_fflush(EpochId e, FILE* file)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = FFLUSH;
  sop->base.result = 0;
  sop->file.fp = file;
  sop->buffer = 0;
  sop->arg1.u32 = 0;

  issue(sop,0);

  return 0;
}

int __txio_fclose(EpochId e, FILE* file)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = FCLOSE;
  sop->base.result = 0;
  sop->file.fp = file;
  sop->buffer = 0;
  sop->arg1.u32 = 0;

  issue(sop,0);

  return 0;
}

int __txio_close(EpochId e, int fd)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = CLOSE;
  sop->base.result = 0;
  sop->file.i32 = fd;
  sop->buffer = 0;
  sop->arg1.u32 = 0;

  issue(sop,0);

  return 0;
}
void __txio_exit(EpochId e, int r)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = EXIT;
  sop->base.result = 0;
  sop->file.fp = 0;
  sop->buffer = 0;
  sop->arg1.u32 = r;

  issue(sop,1);
}

void __txio___assert_fail(EpochId e, const char *msg, const char *filename, uint32_t lineno, const char *fcnname)
{
  __txio_exit(e,1);
}

void __txio_abort(EpochId e)
{
  __txio_exit(e,1);
}

void __txio_llvm_trap(EpochId e)
{
  __txio_abort(e);
}

int __txio_unlink(EpochId e, const char *fn)
{
  return __txio_remove(e,fn);
}

int __txio_remove(EpochId e, const char *fn)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = REMOVE;
  sop->base.result = 0;
  sop->file.fp = 0;
  sop->buffer = strdup(fn);
  sop->arg1.u32 = 0;

  issue(sop,0);

  return 0;
}

int __txio_perror(EpochId e, const char *fmt)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = PERROR;
  sop->base.result = 0;
  sop->file.fp = 0;
  sop->buffer = strdup(fmt);
  sop->arg1.u32 = 0;

  issue(sop,1);

  return 0;
}

int __txio_fseek(EpochId e, FILE *file, long offset, int whence)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = FSEEK;
  sop->base.result = 0;
  sop->file.fp = file;
  sop->buffer = 0;
  sop->arg1.i32 = offset;
  sop->arg2.i32 = whence;

  issue(sop,0);

  return 0;
}

int __txio_lseek(EpochId e, int fd, long offset, int whence)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = LSEEK;
  sop->base.result = 0;
  sop->file.i32 = fd;
  sop->buffer = 0;
  sop->arg1.i32 = offset;
  sop->arg2.i32 = whence;

  issue(sop,0);

  return 0;
}

void __txio_rewind(EpochId e, FILE *file)
{
  __txio_fseek(e,file, 0L, SEEK_SET);
}

int __txio_open(EpochId e, const char *name, int mode, ...)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = OPEN;
  sop->base.result = 0;
  sop->file.fp = 0;
  sop->buffer = strdup(name);
  sop->arg1.i32 = mode;

  Scalar rval = issue(sop,1);
  return rval.i32;
}

FILE *__txio_fopen(EpochId e, const char *name, const char *mode)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = FOPEN;
  sop->base.result = 0;
  sop->file.fp = 0;
  sop->buffer = strdup(name);
  sop->arg1.cptr = mode;

  Scalar rval = issue(sop,1);
  return rval.fp;
}

int __txio_read(EpochId e, int fd, void *buf, int len)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = READ;
  sop->base.result = 0;
  sop->file.i32 = fd;
  sop->buffer = 0;
  sop->arg1.vptr = buf;
  sop->arg2.i32 = len;

  Scalar rval = issue(sop,1);
  return rval.i32;
}

size_t __txio_fread(EpochId e, void *buf, size_t s, size_t nm, FILE* fp)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = FREAD;
  sop->base.result = 0;
  sop->file.fp = fp;
  sop->buffer = 0;
  sop->arg1.vptr = buf;
  sop->arg2.i32 = s;
  sop->arg3.i32 = nm;

  Scalar rval = issue(sop,1);
  return rval.i32;
}



int __txio_fgetc(EpochId e, FILE *fp)
{
  char buff;

  if( 1 == __txio_fread(e, &buff, 1, 1, fp) )
    return (int) buff;
  else
    return EOF;
}

int __txio__IO_getc(EpochId e, FILE *fp)
{
  return __txio_fgetc(e,fp);
}

int __txio_getc(EpochId e, FILE *fp)
{
  return __txio_fgetc(e,fp);
}

int __txio_getchar(EpochId e)
{
  return __txio_fgetc(e, stdin);
}


int __txio___fxstat(EpochId e, int ty, int fd, struct stat *buf)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = FXSTAT;
  sop->base.result = 0;
  sop->file.i32 = fd;
  sop->buffer = 0;
  sop->arg1.i32 = ty;
  sop->arg2.vptr = buf;

  Scalar rval = issue(sop,1);
  return rval.i32;
}

int __txio___xstat(EpochId e, int ty, const char *fn, struct stat *buf)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  // construct a suspended operation object.
  sop->base.op = XSTAT;
  sop->base.result = 0;
  sop->file.i32 = 0;
  sop->buffer = strdup(fn);
  sop->arg1.i32 = ty;
  sop->arg2.vptr = buf;

  Scalar rval = issue(sop,1);
  return rval.i32;
}



char *__txio_fgets(EpochId e, char *buf, int len, FILE *stream)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  sop->base.op = FGETS;
  sop->base.result = 0;
  sop->file.fp = stream;
  sop->buffer = 0;
  sop->arg1.vptr = (void*)buf;
  sop->arg2.i32 = len;

  Scalar rval = issue(sop,1);
  return (char*)rval.vptr;
}


int __txio_ferror(EpochId e, FILE *stream)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  sop->base.op = FERROR;
  sop->base.result = 0;
  sop->file.fp = stream;
  sop->buffer = 0;

  Scalar rval = issue(sop,1);
  return rval.i32;
}


void __txio___deferred_store_i32(EpochId e, uint32_t *ptr, uint32_t val)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = MEM_STORE;
  sop->buffer = 0;
  sop->arg1.i32 = 32;
  sop->arg2.u32ptr = ptr;
  sop->arg3.u32 = val;

  issue(sop,0);
}

void __txio___deferred_store_i64(EpochId e, uint64_t *ptr, uint64_t val)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = MEM_STORE;
  sop->buffer = 0;
  sop->arg1.i32 = 64;
  sop->arg2.u64ptr = ptr;
  sop->arg3.u64 = val;

  issue(sop,0);
}

void __txio___deferred_store_float(EpochId e, float *ptr, float val)
{
  assert(sizeof(float)==sizeof(uint32_t));
  Scalar pointer, value;
  pointer.fptr = ptr;
  value.f = val;
  __txio___deferred_store_i32(e,pointer.u32ptr,value.u32);
}

void __txio___deferred_store_double(EpochId e, double *ptr, double val)
{
  assert(sizeof(double)==sizeof(uint64_t));
  Scalar pointer,value;
  pointer.dptr = ptr;
  value.d = val;
  __txio___deferred_store_i64(e,pointer.u64ptr,value.u64);
}


uint32_t __txio___deferred_load_i32(EpochId e, uint32_t *ptr)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = MEM_LOAD;
  sop->buffer = 0;
  sop->arg1.i32 = 32;
  sop->arg2.u32ptr = ptr;

  Scalar rval = issue(sop,1);
  return rval.u32;
}


uint64_t __txio___deferred_load_i64(EpochId e, uint64_t *ptr)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = MEM_LOAD;
  sop->buffer = 0;
  sop->arg1.i32 = 64;
  sop->arg2.u64ptr = ptr;

  Scalar rval = issue(sop,1);
  return rval.u64;
}

float __txio___deferred_load_float(EpochId e, float *ptr)
{
  assert(sizeof(float)==sizeof(uint32_t));
  Scalar pointer,value;
  pointer.fptr = ptr;
  value.u32 = __txio___deferred_load_i32(e,pointer.u32ptr);
  return value.f;
}

double __txio___deferred_load_double(EpochId e, double *ptr)
{
  assert(sizeof(double)==sizeof(uint64_t));
  Scalar pointer,value;
  pointer.dptr = ptr;
  value.u64 = __txio___deferred_load_i64(e,pointer.u64ptr);
  return  value.d;
}

void __txio___deferred_add_i32(EpochId e, uint32_t *ptr, uint32_t val)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = MEM_ADD;
  sop->buffer = 0;
  sop->arg1.i32 = 32;
  sop->arg2.u32ptr = ptr;
  sop->arg3.u32 = val;

  issue(sop,0);
}

void __txio___deferred_add_i64(EpochId e, uint64_t *ptr, uint64_t val)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = MEM_ADD;
  sop->buffer = 0;
  sop->arg1.i32 = 64;
  sop->arg2.u64ptr = ptr;
  sop->arg3.u64 = val;

  issue(sop,0);
}

void __txio___deferred_add_float(EpochId e, float *ptr, float val)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = MEM_ADD;
  sop->buffer = 0;
  sop->arg1.i32 = -32;
  sop->arg2.fptr = ptr;
  sop->arg3.f = val;

  issue(sop,0);
}

void __txio___deferred_add_double(EpochId e, double *ptr, double val)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = MEM_ADD;
  sop->buffer = 0;
  sop->arg1.i32 = -64;
  sop->arg2.dptr = ptr;
  sop->arg3.d = val;

  issue(sop,0);
}

void __txio_fadd_vec(EpochId e, float *dst, float *src, unsigned n)
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert(sop->base.op != SUB_TX );

  sop->base.op = FADD_VEC;
  sop->buffer = 0;
  sop->arg2.fptr = dst;
  sop->arg2.fptr = src;
  sop->arg3.u32 = n;

  issue(sop,0);
}

void __txio___deferred_call(EpochId e, void (*fcn)(void*), void *arg )
{
  assert(e);
  SusOp *sop = (SusOp*)e;
  assert( sop->base.op != SUB_TX );

  sop->base.op = CALL;
  sop->buffer = 0;
  sop->arg1.fcn = fcn;
  sop->arg2.vptr = arg;

  issue(sop,0);
}



