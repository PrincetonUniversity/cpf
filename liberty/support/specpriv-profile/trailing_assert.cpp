#include "trailing_assert.h"

static pid_t leading_thread_pid;

void __prof_capture_leading_thread_pid(void)
{
  leading_thread_pid = getpid();
}

pid_t __prof_get_leading_thread_pid(void)
{
  return leading_thread_pid;
}
