// A variant of the popular assert() macro
// to be used in the profiler's trailing thread.
// The idea is that if an assert fails in the
// trailing thread, then we also want to kill the leading
// thread.
#ifndef TRAILING_ASSERT_H
#define TRAILING_ASSERT_H

#include <unistd.h>
#include <signal.h>
#include <cassert>

// disable assert fail if not in debug
#ifndef NDEBUG
#define trailing_assert(expr)                                                 \
  do                                                                          \
  {                                                                           \
    if( !(expr) )                                                             \
    {                                                                         \
      kill(__prof_get_leading_thread_pid(), SIGTERM);                         \
      sleep(1);                                                               \
      kill(__prof_get_leading_thread_pid(), SIGSTOP);                         \
      kill(__prof_get_leading_thread_pid(), SIGKILL);                         \
      __assert_fail( __STRING(expr), __FILE__, __LINE__, __ASSERT_FUNCTION);  \
    }                                                                         \
  } while(0)
#else
#define trailing_assert(expr)                                                 \
  do                                                                          \
  {                                                                           \
    if( !(expr) )                                                             \
    {                                                                         \
      kill(__prof_get_leading_thread_pid(), SIGTERM);                         \
      sleep(1);                                                               \
      kill(__prof_get_leading_thread_pid(), SIGSTOP);                         \
      kill(__prof_get_leading_thread_pid(), SIGKILL);                         \
    }                                                                         \
  } while(0)
#endif

void __prof_capture_leading_thread_pid(void);
pid_t __prof_get_leading_thread_pid(void);


#endif

