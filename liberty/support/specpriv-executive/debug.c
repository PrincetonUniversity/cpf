#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "api.h"
#include "constants.h"
#include "debug.h"

#if (DEBUG_ON || PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)
static FILE* debugfps[MAX_WORKERS+1];

static bool __dbgwithstderr = false;

void dbgwithstderr()
{
  __dbgwithstderr = true;
}

static inline FILE* get_debug_fp(void)
{
  if (__dbgwithstderr) return stderr;

  Wid wid = PREFIX(my_worker_id)();

  assert( (wid < MAX_WORKERS) || (wid == MAIN_PROCESS_WID) );

  if ( wid == MAIN_PROCESS_WID )
    wid = MAX_WORKERS;

  return debugfps[ wid ];
}
#endif

void init_debug(unsigned num_all_workers)
{
#if (DEBUG_ON || PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)
  unsigned i = 0;
  for ( ; i < num_all_workers ; i++)
  {
    if ( debugfps[i] == NULL )
    {
      char buf[80];
      sprintf(buf, "__specpriv_debug_%d.out", i);
      debugfps[i] = fopen(buf, "w");
    }
  }

  // for a main process

  if ( debugfps[ MAX_WORKERS ] == NULL )
  {
    char buf[80];
    sprintf(buf, "__specpriv_debug_%d.out", MAX_WORKERS);
    debugfps[ MAX_WORKERS ] = fopen(buf, "w");
  }
#endif
}

size_t DBG(const char* format, ...)
{
#if (DEBUG_ON)
  FILE* fp = get_debug_fp();
  if ( !fp )
    return (size_t)-1;

  va_list ap;
  int     size;
  va_start(ap, format);
  size = vfprintf(fp, format, ap);
  va_end(ap);
  fflush(fp);

  return (size_t)size;
#else
  return 0;
#endif
}

size_t __specpriv_debugprintf(const char* format, ...)
{
#if (DEBUG_ON)
  FILE* fp = get_debug_fp();
  if ( !fp )
    return (size_t)-1;

  va_list ap;
  int     size;
  va_start(ap, format);
  size = vfprintf(fp, format, ap);
  va_end(ap);
  fflush(fp);

  return (size_t)size;
#else
  return 0;
#endif
}

/*
void __specpriv_roidebug_init()
{
#if (DEBUG_ON)
  if ( debugfps[ 0 ] == NULL )
  {
    char buf[80];
    sprintf(buf, "__specpriv_debug_%d.out", 0);
    debugfps[ 0 ] = fopen(buf, "w");
  }
#endif
}
*/

size_t PROFDUMP(const char* format, ...)
{
#if (PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)
  FILE* fp = get_debug_fp();
  if ( !fp )
    return (size_t)-1;

  va_list ap;
  int     size;
  va_start(ap, format);
  size = vfprintf(fp, format, ap);
  va_end(ap);
  fflush(fp);

  return (size_t)size;
#else
  return 0;
#endif
}
