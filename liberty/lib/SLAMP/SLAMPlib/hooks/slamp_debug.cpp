#include "slamp_debug.h"

#include <assert.h>

#include <fstream>
#include <iostream>
#include <string>
#include <sstream>

#include <tr1/unordered_map>
#include <set>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

template <class T> inline std::string to_string (const T& t) {
  std::stringstream ss;
  ss << t;
  return ss.str();
}

namespace slamp
{

std::tr1::unordered_map<TS*, std::string> callstackmap;
std::set<KEY, KEYComp> traced;

static std::string getcallstack()
{
  unw_cursor_t cursor;
  unw_context_t uc;
  unw_word_t ip;

  unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);

  // move up once because current stack frame (within handler) is meaningless

  assert(unw_step(&cursor) > 0);
  unw_get_reg(&cursor, UNW_REG_IP, &ip);

  std::string ctxt;

  while (unw_step(&cursor) > 0)
  {
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    ctxt += (to_string<void*>( (void*)ip ) + "::"); 
  }

  return ctxt;
}

void capturestorecallstack( TS* pts )
{
#if CTXTDEBUG
  callstackmap[ pts ] = getcallstack();
#endif
}

void dumpdependencecallstack( TS* pts, KEY key )
{
  if ( traced.count(key) && !(key.cross) )
    return;

  std::cerr << key.src << "->" << key.dst << " cross " << key.cross << "\n";
  std::cerr << "\t" << "SRC: " << callstackmap[ pts ] << "\n";
  std::cerr << "\t" << "DST: " << getcallstack() << "\n";

  traced.insert( key );
}

}
