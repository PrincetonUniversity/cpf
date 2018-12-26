#include <unistd.h>
#include <sys/mman.h>

#include "api.h"
#include "internals/constants.h"
#include "internals/profile.h"
#include "internals/smtx/procmapsinfo.h"
#include "internals/smtx/protection.h"
#include "internals/smtx/separation.h"
#include "internals/smtx/smtx.h"
#include "internals/smtx/malloc.h"

extern char etext;
extern char end;

namespace specpriv_smtx
{

char* stack_begin = NULL;
char* stack_bound = NULL;

std::vector<uint8_t*>* shadow_globals = NULL;
std::vector<uint8_t*>* shadow_stacks = NULL;
std::set<uint8_t*>*    shadow_heaps = NULL;

std::tr1::unordered_map<void*, size_t>* heap_size_map;

Wid try_commit_begin = 0;

void PREFIX(init)()
{
  // initialize data structures

  shadow_globals = new std::vector<uint8_t*>();
  shadow_stacks = new std::vector<uint8_t*>();
  shadow_heaps = new std::set<uint8_t*>();
  heap_size_map = new std::tr1::unordered_map<void*, size_t>();

  int prot = PROT_WRITE|PROT_READ;
  int flags = MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED;

  procmaps::ProcMapsInfo procmapsinfo;

  // initialize shadow for globals

  char*  heap_bound = (char*)( ROUND_UP(&end, (size_t)PAGE_SIZE) );
  char*  heap_begin = (char*)( (size_t)(&etext) & PAGE_MASK );

  for ( char* i=heap_begin ; i<heap_bound ; i+=PAGE_SIZE)
  {
    if ( !procmapsinfo.doesExist( (void*)i ) )
      continue;

    void* shadow = mmap( (void*)GET_SHADOW_OF(i), PAGE_SIZE, prot, flags, -1, 0);
    assert( shadow != MAP_FAILED && "Error: shadow allocation for globals failed\n" );

    if ( procmapsinfo.isReadOnly( (void*)i ) )
      continue;

    shadow_globals->push_back( (uint8_t*)shadow );
  }

  // initialize shadow for stack

  std::pair <void*, void*> stack_region = procmapsinfo.getStackRegion();

  stack_begin = (char*)( (size_t)stack_region.first  & PAGE_MASK );
  stack_bound = (char*)( ROUND_UP((size_t)stack_region.second, (size_t)PAGE_SIZE) );

  for ( char* i=stack_begin ; i<stack_bound ; i+=PAGE_SIZE)
  {
    void* shadow = mmap( (void*)GET_SHADOW_OF(i), PAGE_SIZE, prot, flags, -1, 0);
    assert( shadow != MAP_FAILED && "Error: shadow allocation for stacks failed\n" );
    shadow_stacks->push_back( (uint8_t*)shadow );
  }

  ver_malloc_init();

  register_base_handler();
}

static unsigned long long skippables = 0;
static unsigned long long nonskippables = 0;

void PREFIX(count_skippables)()
{
  skippables++;
}

void PREFIX(count_nonskippables)()
{
  nonskippables++;
}

#if 0
__attribute__((destructor)) static void __localdestructor()
{
  fprintf(stderr, "\n\nskippables %lu nonskippables %lu\n", skippables, nonskippables);
}
#endif

void PREFIX(fini)()
{

  ver_malloc_fini();

  for ( unsigned i=0,e=shadow_globals->size() ; i<e ; i++)
    munmap( (void*)((*shadow_globals)[i]), PAGE_SIZE );

  // call fini functions in other modules

  delete shadow_globals;
  delete shadow_heaps;
  delete shadow_stacks;
  delete heap_size_map;

  // TODO: unmap shadow stack
}

void set_try_commit_begin(Wid wid)
{
  assert( (try_commit_begin == 0) && "try_commit_begin not null\n");
  try_commit_begin = wid;
}

void reset_try_commit_begin()
{
  try_commit_begin = 0;
}


}
