#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "config.h"
#include "profiler.h"
#include "trailing_assert.h"

#ifdef __linux__
#include <setjmp.h>
#endif

#if TIMER
#include <iostream>
#endif

#include <csignal>

extern "C"
{
#include "sw_queue_astream.h"

//static sw_queue_t queue;
static SW_Queue the_queue;

#define CONSUME         sq_consume(the_queue);
#define PRODUCE(x)      sq_produce(the_queue,(uint64_t)x);

#define PRODUCE_2(x,y)  PRODUCE( (((uint64_t)x)<<32) | (uint32_t)(y) )
#define CONSUME_2(x,y)  do { uint64_t tmp = CONSUME; x = (uint32_t)(tmp>>32); y = (uint32_t) tmp; } while(0)


// Consume and execute one message from the queue.
// Return false if program is over.
static bool process_message()
{
  static const char *last_loop=0;
  Profiler &prof = Profiler::getInstance();

  uint32_t code,second;
  CONSUME_2(code, second);

  const char *name=0;
  void *ptr=0, *ptr2=0;
  uint64_t size=0;

  switch(code)
  {
    case 0:
      // malloc
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      size = second;
      prof.malloc(name,ptr,size);
      break;
    case 1:
      // free
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      prof.free(name,ptr, (second==1) );
      break;
    case 2:
      // report constant
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      size = second;
      prof.report_constant(name,ptr,size);
      break;
    case 3:
      // report global
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      size = second;
      prof.report_global(name,ptr,size);
      break;
    case 4:
      // report stack
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      size = second;
      prof.report_stack(name,ptr,1,size);
      break;
    case 5:
      // begin function
      name = (const char*) CONSUME;
      prof.begin_function(name);
      break;
    case 6:
      // end function
      prof.end_function(0);
      break;
    case 7:
      // begin iter
      last_loop = (const char*) CONSUME;
      prof.begin_iter(last_loop);
      break;
    case 8:
      // end iter
      prof.end_iter(0);
      break;
    case 9:
      // find underlying object
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      prof.find_underlying_object(name,ptr);
      break;
    case 10:
      // predict int
      name = (const char*) CONSUME;
      size = CONSUME;
      prof.predict_int(name,size);
      break;
    case 11:
      // predict ptr
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      prof.predict_ptr(name,ptr);
      break;
    case 12:
      // end
      return true;
    case 13:
      // realloc
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      ptr2 = (void*) CONSUME;
      size = second;
      prof.realloc(name,ptr,ptr2,size);
      break;
    case 14:
      // assert in-bounds
      name = (const char*) CONSUME;
      ptr = (void*) CONSUME;
      ptr2 = (void*) CONSUME;
      prof.assert_in_bounds(name,ptr,ptr2);
      break;
    case 15:
      // possible object leak
      name = (const char *) CONSUME;
      prof.possible_allocation_leak(name);
      break;
    case 16:
      // pointer residues
      name = (const char *) CONSUME;
      ptr = (void *) (uint64_t) second;
      prof.pointer_residue(name,ptr);
      break;
    case 17:
      // predict integer (with 32-bit value)
      name = (const char *) CONSUME;
      size = (uint64_t)second;
      prof.predict_int(name,size);
      break;
    case 18:
      // begin iteration (with same loop name)
      prof.begin_iter(last_loop);
      break;

    default:
      break;
  }
  return false;
}

#define PIDFILE   "specpriv.profile.trailing.thread.pid"

// Remove my PID file.
void remove_pid()
{
  unlink(PIDFILE);
}

// The profiler thread
static void *thread_loop(void)
{
  // Write a PID file.
  {
    std::ofstream pidout(PIDFILE);
    pidout << getpid();
  }
  atexit( &remove_pid );

  Profiler &prof = Profiler::getInstance();

  prof.begin();

#if TIMER
  unsigned nEvents = 0;
#endif

#ifdef DEBUG_SPEED
  std::ofstream log_dump("wtf.dump");
  log_dump << "WTF\n";
  unsigned nEvents = 0;
#endif
  while( !process_message() )
  {
    //printf("Processed a message.\n");



#ifdef DEBUG_SPEED
    nEvents++;
    if (nEvents == 2000000) {
      prof.test_print(log_dump);
      log_dump << std::flush;

      nEvents = 0;
    }
#endif

#if TIMER
    ++nEvents;
    if( nEvents == 200000000 )
    {
      prof.timing_stats( std::cout );
      nEvents = 0;
    }
#endif
  }

  prof.end();
  exit(0);
}

static void end_helper()
{
  PRODUCE_2(12,0);
  sq_flushQueue(the_queue);
}

static void signal_helper(int signum)
{
  fprintf(stderr, "Process-under-test will die because of signal %d\n", signum);
  fflush(stderr);

  // We might be in the middle of a packet...
  // Send enough that we will necessarily get the message across.
  PRODUCE_2(12,0);
  PRODUCE_2(12,0);
  PRODUCE_2(12,0);
  PRODUCE_2(12,0);
  PRODUCE_2(12,0);
  PRODUCE_2(12,0);
  sq_flushQueue(the_queue);

  _exit(128+signum);
}

// These functions are C-friendly front-ends
// They each produce their values over a queue to the profiler thread
// who in turn executes them off the critical path.
void __prof_begin()
{
  the_queue = sq_createQueue();

  __prof_capture_leading_thread_pid();

  if( fork() == 0 )
    thread_loop();

  // Trap any signal which will, by default, terminate the process.
  struct sigaction action;
  action.sa_handler = *signal_helper;
  action.sa_flags = 0;
  sigemptyset( & action.sa_mask );
  // According to 'man 7 signal', this is the
  // list of signals whose default behavior is TERM or CORE
  sigaction( SIGHUP, &action, 0 );
  sigaction( SIGINT, &action, 0 );
  sigaction( SIGQUIT, &action, 0 );
  sigaction( SIGILL, &action, 0 );
  sigaction( SIGABRT, &action, 0 );
  sigaction( SIGFPE, &action, 0 );
  sigaction( SIGSEGV, &action, 0 );
  sigaction( SIGPIPE, &action, 0 );
  sigaction( SIGALRM, &action, 0 );
  sigaction( SIGTERM, &action, 0 );
  sigaction( SIGUSR1, &action, 0 );
  sigaction( SIGUSR2, &action, 0 );
  sigaction( SIGBUS, &action, 0 );
  sigaction( SIGPOLL, &action, 0 );
  sigaction( SIGPROF, &action, 0 );
  sigaction( SIGSYS, &action, 0 );
  sigaction( SIGTRAP, &action, 0 );
  sigaction( SIGVTALRM, &action, 0 );
  sigaction( SIGXCPU, &action, 0 );
  sigaction( SIGXFSZ, &action, 0 );
  sigaction( SIGIOT, &action, 0 );
//  sigaction( SIGEMT, &action, 0 );
  sigaction( SIGSTKFLT, &action, 0 );
  sigaction( SIGIO, &action, 0 );
  sigaction( SIGPWR, &action, 0 );
//  sigaction( SIGLOST, &action, 0 );
  sigaction( SIGSYS, &action, 0 );

  atexit( &end_helper );
}

void __prof_malloc(const char *name, void *ptr, uint64_t size)
{
  if( ptr )
  {
    PRODUCE_2(0,size);
    PRODUCE(name);
    PRODUCE(ptr);
  }
}

void __prof_free(const char *name, void *ptr)
{
  if( ptr )
  {
    PRODUCE_2(1,0);
    PRODUCE(name);
    PRODUCE(ptr);
  }
}

void __prof_free_stack(const char *name, void *ptr)
{
  if( ptr )
  {
    PRODUCE_2(1,1);
    PRODUCE(name);
    PRODUCE(ptr);
  }
}

void __prof_report_constant(const char *name, void *base, uint64_t size)
{
  PRODUCE_2(2,size);
  PRODUCE(name);
  PRODUCE(base);
}

void __prof_report_global(const char *name, void *base, uint64_t size)
{
  PRODUCE_2(3,size);
  PRODUCE(name);
  PRODUCE(base);
}

void __prof_report_stack(const char *name, void *base, uint64_t array_size, uint64_t elt_size)
{
  if( 0 == array_size )
    array_size = 1;
  const uint64_t size = array_size*elt_size;

  PRODUCE_2(4,size);
  PRODUCE(name);
  PRODUCE(base);
}

void __prof_begin_function(const char *name)
{
  PRODUCE_2(5,0);
  PRODUCE(name);
}

void __prof_end_function(const char *name)
{
  PRODUCE_2(6,0);
}

void __prof_report_constant_string(const char *name, void *ptr)
{
  if( ptr == 0 )
    return;

  const unsigned len = strlen( (const char*)ptr );
  __prof_report_constant(name, ptr, 1+len);
}

#define ARGV_NAME   "UNMANAGED argv"

void __prof_manage_argv(uint32_t argc, const char **argv)
{
  __prof_report_constant(ARGV_NAME, (void*)argv, argc * sizeof(void*) );
  for(unsigned i=0; i<argc; ++i)
    __prof_report_constant_string(ARGV_NAME, (void*)argv[i]);
}

void __prof_unmanage_argv(uint32_t, const char **argv)
{
  // nothing to do.
}


void __prof_begin_iter(const char *name)
{
  static const char *last_loop_sent = 0;

  if( name == last_loop_sent )
  {
    // Reduce communication in common case.
    PRODUCE_2(18,0);
  }
  else
  {
    PRODUCE_2(7,0);
    PRODUCE(name);
    last_loop_sent = name;
  }
}

void __prof_end_iter(const char *name)
{
  PRODUCE_2(8,0);
}

void __prof_find_underlying_object(const char *name, void *ptr)
{
  PRODUCE_2(9,0);
  PRODUCE(name);
  PRODUCE(ptr);
}

void __prof_predict_int(const char *name, uint64_t value)
{
  uint32_t truncate = (uint32_t)value;
  if( truncate == value )
  {
    // special case for predicting 32-bit values
    PRODUCE_2(17,truncate);
    PRODUCE(name);
  }
  else
  {
    PRODUCE_2(10,0);
    PRODUCE(name);
    PRODUCE(value);
  }
}

void __prof_predict_ptr(const char *name, void *ptr)
{
  PRODUCE_2(11,0);
  PRODUCE(name);
  PRODUCE(ptr);
}

// Load 'size_bytes' bytes from pointer 'ptr' and return
// a zero-extended representation of the result.
uint64_t __prof_unsafe_load(void *ptr, unsigned size_bytes)
{
  switch( size_bytes )
  {
    case 1:
      return (uint64_t) * (uint8_t*) ptr;
    case 2:
      return (uint64_t) * (uint16_t*) ptr;
    case 4:
      return (uint64_t) * (uint32_t*) ptr;
    case 8:
      return (uint64_t) * (uint64_t*) ptr;
  }

  fprintf(stderr, "Bad size %d\n", size_bytes);
  _exit(1);
}

#ifdef __linux__
  // Not portable, since most systems make no guarantee about
  // system state after a segmentation fault.
  // However, this works on linux and is much faster.
  static sigjmp_buf __prof_return_from_segfault_handler;
  static struct sigaction old_handler;
  static struct sigaction new_handler;

  static void __prof_segfault_handler(int d)
  {
    // We reached this point because loading the pointer
    // caused a segfault.
    siglongjmp(__prof_return_from_segfault_handler,1);
  }

  static bool __prof_safe_load(void *ptr, uint64_t *value_out, unsigned size_bytes)
  {
    if( sigsetjmp(__prof_return_from_segfault_handler, 1) == 0 )
    {
      // Install a custom segfault (SIGSEGV) handler.
      new_handler.sa_handler = __prof_segfault_handler;
      sigemptyset( & new_handler.sa_mask );
      new_handler.sa_flags = 0;
      sigaction(SIGSEGV, &new_handler, &old_handler);

      // This operation may segfault:
      uint64_t value = __prof_unsafe_load(ptr, size_bytes);

      // If we reached this point, then no segfault occurred.
      sigaction(SIGSEGV, &old_handler, 0);
      *value_out = value;
      return true;
    }
    else
    {
      // segfault handler returns to here.
      sigaction(SIGSEGV, &old_handler, 0);
      return false;
    }
  }


#else
  // -------- Portable method to load an unsafe pointer -----
  static bool __prof_safe_load(void *ptr, uint64_t *value_out, unsigned size_bytes)
  {
    pid_t result = fork();
    assert( result >= 0 && "fork() failed");
    if( result == 0 )
    {
      // I am the child process
      __prof_unsafe_load(ptr,size_bytes);
      // No segfault: return a successful exit code.
      _exit(0);
    }

    // I am the parent process
    int status = ~0;
    waitpid(result, &status, 0);
    if( status == 0 )
    {
      // The child was able to perform this load
      // without segfaulting.
      *value_out = __prof_unsafe_load(ptr,size_bytes);
      return true;
    }
    else
    {
      // The child experienced a segfault while
      // performing this load.
      return false;
    }
  }
#endif

void __prof_predict_int_load(const char *name, void *ptr, uint32_t size)
{
  if( size > sizeof(uint64_t) )
    return; // Ignore.

  uint64_t load;
  if( __prof_safe_load(ptr, &load, size) )
    __prof_predict_int(name, load);
//  else
//    fprintf(stderr, "Warning: __prof_predict_int_load(name=%s, ptr=%p, size=%d) segfault\n", name, ptr, size);
}
void __prof_predict_ptr_load(const char *name, void *ptr)
{
  uint64_t load;
  if( __prof_safe_load(ptr, &load, sizeof(void*) ) )
    __prof_predict_ptr(name, (void*)load);
//  else
//    fprintf(stderr, "Warning: __prof_predict_ptr_load(name=%s, ptr=%p, size=%ld) segfault\n", name, ptr, sizeof(void*));
}

void __prof_pointer_residue(const char *name, void *ptr)
{
  // It's okay to truncate ptr to 32 bits in this
  // case, because the profiler is going to truncate
  // it to 4 bits.
  PRODUCE_2(16,(uint32_t)(uint64_t)ptr);
  PRODUCE(name);
}

void __prof_realloc(const char *name, void *old_ptr, void *new_ptr, uint64_t size)
{
  PRODUCE_2(13,size);
  PRODUCE(name);
  PRODUCE(old_ptr);
  PRODUCE(new_ptr);
}

// This is only emitted by the profiler if we are in sanity checking mode.
void __prof_assert_in_bounds(const char *name, void *base, void *derived)
{
  if( base == derived )
    return;

  PRODUCE_2(14,0);
  PRODUCE(name);
  PRODUCE(base);
  PRODUCE(derived);
}

void __prof_possible_allocation_leak(const char *name)
{
  PRODUCE_2(15,0);
  PRODUCE(name);
}

void *__prof_malloc_align16(size_t size)
{
  void *ptr;
  const int rval = posix_memalign(&ptr, 16, size);
  assert( rval == 0 && "posix_memalign failed.");
  return ptr;
}

void *__prof_calloc_align16(size_t a, size_t b)
{
  const size_t size = a*b;
  void *result = __prof_malloc_align16(size);
  memset(result, 0, size);
  return result;
}

void *__prof_realloc_align16(void *old, size_t size)
{
  // Tricky; we did not keep track of the size
  // of the old object.  Thus, we cannot safely
  // copy the old contents to the new.

  void *newObject = realloc(old,size);

  // We have a 50% chance on a 64-bit architecture that
  // realloc will do the right thing; 25% on a 32-bit...
  const bool isAligned = 0 == (( (uint64_t) newObject ) & 0x0f);
  if( isAligned )
    return newObject;

  // realloc() screwed up.  Correct it.
  void *newNewObject = __prof_malloc_align16(size);

  // This is now safe, because we know
  // sizeof( *newNewObject ) >= size.
  memcpy(newNewObject, newObject, size);

  free(newObject);
  return newNewObject;
}

}


