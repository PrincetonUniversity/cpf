#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <jemalloc/jemalloc.h>

#include <set>
#include <map>

#include "api.h"
#include "internals/constants.h"
#include "internals/debug.h"
#include "internals/speculation.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/malloc.h"
#include "internals/smtx/separation.h"

namespace specpriv_smtx
{

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

std::set<unsigned>*  uc_regions = NULL;
std::set<unsigned>*  ver_uc_regions = NULL;
std::set<unsigned>*  ro_regions = NULL;
std::set<unsigned>*  ver_ro_regions = NULL;
std::set<unsigned>*  nrbw_regions = NULL;
std::set<unsigned>*  ver_nrbw_regions = NULL;
std::set<unsigned>** stage_private_regions = NULL;
std::set<unsigned>** ver_stage_private_regions = NULL;

std::map<void*, size_t>* separation_heap_size_map; // global across all heaps

static SeparationHeap*  heaps = 0;
static SeparationHeap** versioned_heaps = 0;

static unsigned nnvh = 0;
static unsigned nvh = 0;

static unsigned separation_alloc_context = 0;

void PREFIX(separation_init)(unsigned num_non_versioned_heaps, unsigned num_versioned_heaps)
{
  nnvh = num_non_versioned_heaps;
  nvh = num_versioned_heaps;

  if (nnvh == 0 && nvh == 0)
    return;

  // separation disabled
  assert( false );

  // initialize region keepers

  uc_regions = new std::set<unsigned>();
  ver_uc_regions = new std::set<unsigned>();
  ro_regions = new std::set<unsigned>();
  ver_ro_regions = new std::set<unsigned>();
  nrbw_regions = new std::set<unsigned>();
  ver_nrbw_regions = new std::set<unsigned>();
  stage_private_regions = (std::set<unsigned>**)malloc( sizeof(std::set<unsigned>*) * MAX_STAGE_NUM );
  ver_stage_private_regions = (std::set<unsigned>**)malloc( sizeof(std::set<unsigned>*) * MAX_STAGE_NUM );

  for (unsigned i = 0 ; i < MAX_STAGE_NUM ; i++)
  {
    stage_private_regions[i] = new std::set<unsigned>();
    ver_stage_private_regions[i] = new std::set<unsigned>();
  }

  separation_heap_size_map = new std::map<void*, size_t>();

  // initialize memory/shadow-memory for separation heaps

  uint64_t begin = SEPARATION_HEAP_BEGIN;
  uint64_t bound = SEPARATION_HEAP_BOUND;
  uint64_t chunksize = SEPARATION_HEAP_CHUNKSIZE;
  uint64_t num_chunks = (bound - begin) / chunksize;

  // initialize heaps

  heaps = (SeparationHeap*)malloc( sizeof(SeparationHeap) * num_non_versioned_heaps );

  unsigned chunks_per_heap = num_chunks / num_non_versioned_heaps;
  unsigned rem = num_chunks % num_non_versioned_heaps;
  uint64_t n = SEPARATION_HEAP_BEGIN;

  for (unsigned i = 0 ; i < num_non_versioned_heaps ; i++)
  {
    uint64_t heapsize = chunks_per_heap * chunksize;
    if (i < rem) heapsize += chunksize;

    heaps[i].begin = heaps[i].next = heaps[i].nextchunk = n;
    heaps[i].end = n + heapsize;

    // fprintf(stderr, "%u begin %lx end %lx size %lx\n", i, n, n+heapsize, heapsize);
    n += heapsize;
  }

  // initialized versioned heaps

  if (num_versioned_heaps)
  {
    begin = VER_SEPARATION_HEAP_BEGIN;
    bound = VER_SEPARATION_HEAP_BOUND;
    num_chunks = (bound-begin) / chunksize;

    versioned_heaps = (SeparationHeap**)malloc( sizeof(SeparationHeap*) * MAX_WORKERS);

    unsigned total_heaps = num_versioned_heaps * MAX_WORKERS;
    chunks_per_heap = num_chunks / total_heaps;
    rem = num_chunks % total_heaps;
    n = VER_SEPARATION_HEAP_BEGIN;

    unsigned idx = 0;
    for (unsigned i = 0 ; i < MAX_WORKERS ; i++)
    {
      versioned_heaps[i] = (SeparationHeap*)malloc( sizeof(SeparationHeap) * num_versioned_heaps );

      for (unsigned j = 0 ; j < num_versioned_heaps ; j++)
      {
        size_t sz = chunks_per_heap * chunksize;
        if (idx < rem) sz += chunksize;

        versioned_heaps[i][j].begin = versioned_heaps[i][j].next = versioned_heaps[i][j].nextchunk = n;
        versioned_heaps[i][j].end = n + sz;

        //fprintf(stderr, "%u %u begin %lx end %lx size %lx\n", i, j, n, n+sz, sz);

        n += sz;
        idx += 1;
      }
    }
  }
}

void PREFIX(separation_fini)(unsigned num_non_versioned_heaps, unsigned num_versioned_heaps)
{
  if (num_non_versioned_heaps == 0 && num_versioned_heaps == 0)
    return;

  for (unsigned i = 0 ; i < MAX_STAGE_NUM ; i++)
  {
    delete stage_private_regions[i];
    delete ver_stage_private_regions[i];
  }
  free(stage_private_regions);
  free(ver_stage_private_regions);

  delete uc_regions;
  delete ver_uc_regions;
  delete ro_regions;
  delete ver_ro_regions;
  delete nrbw_regions;
  delete ver_nrbw_regions;

  for (unsigned i = 0 ; i < num_non_versioned_heaps ; i++)
  {
    size_t sz = heaps[i].nextchunk - heaps[i].begin;
    munmap( (void*)heaps[i].begin, sz );
    munmap( (void*)GET_SHADOW_OF(heaps[i].begin), sz );
  }

  free(heaps);

  if (num_versioned_heaps)
  {
    for (unsigned i = 0 ; i < MAX_WORKERS ; i++)
    {
      for (unsigned j = 0 ; j < num_versioned_heaps ; j++)
      {
        size_t sz = versioned_heaps[i][j].nextchunk - versioned_heaps[i][j].begin;
        munmap( (void*)versioned_heaps[i][j].begin, sz );
        munmap( (void*)GET_SHADOW_OF(versioned_heaps[i][j].begin), sz );
      }
    }
    free(versioned_heaps);
  }

}

void PREFIX(clear_separation_heaps)()
{
  uc_regions->clear();
  ver_uc_regions->clear();
  ro_regions->clear();
  ver_ro_regions->clear();
  nrbw_regions->clear();
  ver_nrbw_regions->clear();
  for (unsigned i = 0 ; i < MAX_STAGE_NUM ; i++)
  {
    stage_private_regions[i]->clear();
    ver_stage_private_regions[i]->clear();
  }
}

/*
 * Region registeration
 */

void PREFIX(register_unclassified)(unsigned num, ...)
{
  va_list ap;
  va_start(ap, num);

  for (unsigned i = 0 ; i < num ; i++)
  {
    unsigned heap = va_arg(ap, unsigned);
    uc_regions->insert(heap);
  }

  va_end(ap);
}

void PREFIX(register_versioned_unclassified)(unsigned num, ...)
{
  va_list ap;
  va_start(ap, num);

  for (unsigned i = 0 ; i < num ; i++)
  {
    unsigned heap = va_arg(ap, unsigned);
    ver_uc_regions->insert(heap);
  }

  va_end(ap);
}


void PREFIX(register_ro)(unsigned num, ...)
{
  va_list ap;
  va_start(ap, num);

  for (unsigned i = 0 ; i < num ; i++)
  {
    unsigned heap = va_arg(ap, unsigned);
    ro_regions->insert(heap);
  }

  va_end(ap);
}

void PREFIX(register_versioned_ro)(unsigned num, ...)
{
  va_list ap;
  va_start(ap, num);

  for (unsigned i = 0 ; i < num ; i++)
  {
    unsigned heap = va_arg(ap, unsigned);
    ver_ro_regions->insert(heap);
  }

  va_end(ap);
}

void PREFIX(register_nrbw)(unsigned num, ...)
{
  va_list ap;
  va_start(ap, num);

  for (unsigned i = 0 ; i < num ; i++)
  {
    unsigned heap = va_arg(ap, unsigned);
    nrbw_regions->insert(heap);
  }

  va_end(ap);
}

void PREFIX(register_versioned_nrbw)(unsigned num, ...)
{
  va_list ap;
  va_start(ap, num);

  for (unsigned i = 0 ; i < num ; i++)
  {
    unsigned heap = va_arg(ap, unsigned);
    ver_nrbw_regions->insert(heap);
  }

  va_end(ap);
}

void PREFIX(register_stage_private)(unsigned stage, unsigned num, ...)
{
  va_list ap;
  va_start(ap, num);

  for (unsigned i = 0 ; i < num ; i++)
  {
    unsigned heap = va_arg(ap, unsigned);
    (stage_private_regions[stage])->insert(heap);
  }

  va_end(ap);
}

void PREFIX(register_versioned_stage_private)(unsigned stage, unsigned num, ...)
{
  va_list ap;
  va_start(ap, num);

  for (unsigned i = 0 ; i < num ; i++)
  {
    unsigned heap = va_arg(ap, unsigned);
    (ver_stage_private_regions[stage])->insert(heap);
  }

  va_end(ap);
}

/*
 * Region check
 */

bool is_in_uc(void* page)
{
  if (uc_regions)
  {
    for (std::set<unsigned>::iterator i = uc_regions->begin() ; i != uc_regions->end() ; i++)
    {
      if ((heaps[*i].begin <= (uint64_t)page) && ((uint64_t)page < heaps[*i].next))
        return true;
    }
  }

  if (ver_uc_regions)
  {
    for (std::set<unsigned>::iterator i = ver_uc_regions->begin() ; i != ver_uc_regions->end() ; i++)
    {
      for (unsigned j = 0 ; j < MAX_WORKERS ; j++)
      {
        if ((versioned_heaps[j][*i].begin <= (uint64_t)page) && ((uint64_t)page < versioned_heaps[j][*i].next))
          return true;
      }
    }
  }

  return false;
}

bool is_in_ro(void* page)
{
  if (ro_regions)
  {
    for (std::set<unsigned>::iterator i = ro_regions->begin() ; i != ro_regions->end() ; i++)
    {
      if ((heaps[*i].begin <= (uint64_t)page) && ((uint64_t)page < heaps[*i].next))
        return true;
    }
  }

  if (ver_ro_regions)
  {
    for (std::set<unsigned>::iterator i = ver_ro_regions->begin() ; i != ver_ro_regions->end() ; i++)
    {
      for (unsigned j = 0 ; j < MAX_WORKERS ; j++)
      {
        if ((versioned_heaps[j][*i].begin <= (uint64_t)page) && ((uint64_t)page < versioned_heaps[j][*i].next))
          return true;
      }
    }
  }

  return false;
}

bool is_in_nrbw(void* page)
{
  if (nrbw_regions)
  {
    for (std::set<unsigned>::iterator i = nrbw_regions->begin() ; i != nrbw_regions->end() ; i++)
    {
      if ((heaps[*i].begin <= (uint64_t)page) && ((uint64_t)page < heaps[*i].next))
        return true;
    }
  }

  if (ver_nrbw_regions)
  {
    for (std::set<unsigned>::iterator i = ver_nrbw_regions->begin() ; i != ver_nrbw_regions->end() ; i++)
    {
      for (unsigned j = 0 ; j < MAX_WORKERS ; j++)
      {
        if ((versioned_heaps[j][*i].begin <= (uint64_t)page) && ((uint64_t)page < versioned_heaps[j][*i].next))
          return true;
      }
    }
  }

  return false;
}

int is_in_stage_private(void* page)
{
  for (unsigned j = 0 ; j < MAX_STAGE_NUM ; j++)
  {
    if (stage_private_regions)
    {
      for (std::set<unsigned>::iterator i = (stage_private_regions[j])->begin() ; i != (stage_private_regions[j])->end() ; i++)
      {
        if ((heaps[*i].begin <= (uint64_t)page) && ((uint64_t)page < heaps[*i].next))
          return j;
      }
    }

    if (ver_stage_private_regions)
    {
      for (std::set<unsigned>::iterator i = (ver_stage_private_regions[j])->begin() ; i != (ver_stage_private_regions[j])->end() ; i++)
      {
        for (unsigned w = 0 ; w < MAX_WORKERS ; w++)
        {
          if ((versioned_heaps[w][*i].begin <= (uint64_t)page) && ((uint64_t)page < versioned_heaps[w][*i].next))
            return j;
        }
      }
    }
  }

  return -1;
}

/*
 * Region protection setting
 */

void set_separation_heaps_prot_none()
{
  DBG("set_separation_heaps_prot_none\n");
  for (unsigned i = 0 ; i < nnvh ; i++)
  {
    size_t sz = heaps[i].nextchunk - heaps[i].begin;
    if ( sz && mprotect((void*)(heaps[i].begin), sz, PROT_NONE) == -1 )
    {
      DBG("Error: page %p\n", (void*)(heaps[i].begin));
      handle_error("mprotect failed to reset protection (ros)");
    }
  }

  for (unsigned i = 0 ; i < MAX_WORKERS ; i++)
  {
    for (unsigned j = 0 ; j < nvh ; j++)
    {
      size_t sz = versioned_heaps[i][j].nextchunk - versioned_heaps[i][j].begin;
      if ( sz && mprotect((void*)(versioned_heaps[i][j].begin), sz, PROT_NONE) == -1 )
      {
        DBG("Error: page %p\n", (void*)(versioned_heaps[i][j].begin));
        handle_error("mprotect failed to reset protection (ros)");
      }
    }
  }
}


/*
 * utilities
 */

std::set<unsigned>* get_uc()
{
  return uc_regions;
}

std::set<unsigned>* get_versioned_uc()
{
  return ver_uc_regions;
}

std::set<unsigned>* get_nrbw()
{
  return nrbw_regions;
}

std::set<unsigned>* get_versioned_nrbw()
{
  return ver_nrbw_regions;
}

std::set<unsigned>* get_stage_private(unsigned i)
{
  if (!stage_private_regions) return NULL;
  return (stage_private_regions[i]);
}

std::set<unsigned>* get_versioned_stage_private(unsigned i)
{
  if (!ver_stage_private_regions) return NULL;
  return (ver_stage_private_regions[i]);
}

uint64_t heap_begin(unsigned heap)
{
  return heaps[heap].begin;
}

uint64_t heap_bound(unsigned heap)
{
  return heaps[heap].nextchunk;
}

uint64_t versioned_heap_begin(unsigned wid, unsigned heap)
{
  return versioned_heaps[wid][heap].begin;
}

uint64_t versioned_heap_bound(unsigned wid, unsigned heap)
{
  return versioned_heaps[wid][heap].nextchunk;
}

/*
 * non-versioned heap alloc
 */

static void set_prot_none(void* ptr, size_t size)
{
  size_t begin = (size_t)ptr & PAGE_MASK;
  size_t bound = (((size_t)ptr+size) & PAGE_MASK) + PAGE_SIZE;

  if ( mprotect((void*)(begin), (bound-begin), PROT_NONE) == -1 )
  {
    DBG("Error: mprotect set PROT_NONE failed, page %p\n", (void*)(begin));
    handle_error("mprotect failed to reset protection (set_prot_none)");
  }
}

static void* separation_heap_alloc(size_t size, unsigned heap)
{
  //fprintf(stderr, "separation_heap_alloc, heap %u size %lu\n", heap, size);

  assert( heap < nnvh );

  uint64_t ret, next;

  ret = heaps[heap].next;
  size = (size_t)( ROUND_UP( size, ALIGNMENT ) );
  next = ret + size;

  assert( next < heaps[heap].end );

  while ( heaps[heap].nextchunk < next )
  {
    int      prot = PROT_WRITE | PROT_READ;
    int      flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    unsigned chunksize = SEPARATION_HEAP_CHUNKSIZE;

    void* mem = mmap( (void*)heaps[heap].nextchunk, chunksize, prot, flags, -1, 0);

    if ( mem == MAP_FAILED )
    {
      fprintf(stderr, "separation_heap_alloc, mmap failed\n");
      return NULL;
    }

    void* shadow = mmap( (void*)GET_SHADOW_OF(mem), chunksize, prot, flags, -1, 0);

    if (shadow == MAP_FAILED)
    {
      fprintf(stderr, "separation_heap_alloc, shadow mmap failed\n");
      munmap(mem, chunksize);
      return NULL;
    }

    heaps[heap].nextchunk += chunksize;
  }

  (*separation_heap_size_map)[(void*)ret] = size;

  heaps[heap].next = next;

  //fprintf(stderr, "separation_heap_alloc return %p, size %lu\n", (void*)ret, size);
  return (void*)ret;
}

void* PREFIX(separation_malloc)(size_t size, unsigned heap)
{
  return separation_heap_alloc(size, heap);
}

void* PREFIX(separation_calloc)(size_t num, size_t size, unsigned heap)
{
  size_t sz = num*size;
  void*  ret = separation_heap_alloc(sz, heap);
  memset(ret, 0, sz);

  return ret;
}

void* PREFIX(separation_realloc)(void* ptr, size_t size, unsigned heap)
{
  if (ptr && (size == 0))
  {
    // free. do nothing.
    return NULL;
  }

  void* ret = separation_heap_alloc(size, heap);

  if ( !ptr )
    return ret;

  size_t oldsize = (*separation_heap_size_map)[ptr];
  // copy data
  memcpy(ret, ptr, oldsize);
  // copy shadow
  memcpy((void*)GET_SHADOW_OF(ret), (void*)GET_SHADOW_OF(ptr), oldsize);

  separation_heap_size_map->erase(ptr);

  return ret;
}

void PREFIX(separation_free)(void* ptr, unsigned heap)
{
  if (ptr) separation_heap_size_map->erase(ptr);
}

static void* ver_separation_heap_alloc(size_t size, unsigned wid, unsigned heap)
{
  assert( heap < nvh );

  uint64_t ret, next;

  ret = versioned_heaps[wid][heap].next;
  size = (size_t)( ROUND_UP( size, ALIGNMENT ) );
  next = ret + size;

  assert( next < versioned_heaps[wid][heap].end );

  while ( versioned_heaps[wid][heap].nextchunk < next )
  {
    int      prot = PROT_WRITE | PROT_READ;
    int      flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    unsigned chunksize = SEPARATION_HEAP_CHUNKSIZE;

    void* mem = mmap( (void*)versioned_heaps[wid][heap].nextchunk, chunksize, prot, flags, -1, 0);

    if ( mem == MAP_FAILED )
      return NULL;

    void* shadow = mmap( (void*)GET_SHADOW_OF(mem), chunksize, prot, flags, -1, 0);

    if (shadow == MAP_FAILED)
    {
      munmap(mem, chunksize);
      return NULL;
    }

    versioned_heaps[wid][heap].nextchunk += chunksize;
  }

  (*separation_heap_size_map)[(void*)ret] = size;

  versioned_heaps[wid][heap].next = next;

  // notify to following workers

  buffer_ver_malloc( wid, ret, size, heap );
  broadcast_event( wid, (void*)ret, (uint32_t)size, heap, SEPARATION, ALLOC );

  //set_prot_none((void*)ret, size);
  uint8_t* page = (uint8_t*)(ret & PAGE_MASK);
  uint8_t* shadow = (uint8_t*)(GET_SHADOW_OF(page));
  *shadow |= (0x80);

  return (void*)ret;
}

void* update_ver_separation_malloc(size_t size, unsigned wid, unsigned heap, void* ptr)
{
  assert( heap < nvh );

  uint64_t ret, next;

  ret = versioned_heaps[wid][heap].next;
  size = (size_t)( ROUND_UP( size, ALIGNMENT ) );
  next = ret + size;

  assert( next < versioned_heaps[wid][heap].end );

  if ( versioned_heaps[wid][heap].nextchunk < next )
  {
    int      prot = PROT_WRITE | PROT_READ;
    int      flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    unsigned chunksize = SEPARATION_HEAP_CHUNKSIZE;

    void* mem = mmap( (void*)versioned_heaps[wid][heap].nextchunk, chunksize, prot, flags, -1, 0);

    if ( mem == MAP_FAILED )
    {
      DBG("update_ver_separation_heap failed to allocate the next chunk\n");
      assert(false); // TODO: this is a misprediction
    }

    void* shadow = mmap( (void*)GET_SHADOW_OF(mem), chunksize, prot, flags, -1, 0);

    if (shadow == MAP_FAILED)
    {
      DBG("update_ver_separation_heap failed to allocate a shadow for the next chunk\n");
      munmap(mem, chunksize);
      assert(false); // TODO: this is a misprediction
    }

    versioned_heaps[wid][heap].nextchunk += chunksize;
  }

  versioned_heaps[wid][heap].next = next;

  return (void*)ret;
}

void* PREFIX(ver_separation_malloc)(size_t size, unsigned heap)
{
  return ver_separation_heap_alloc(size, PREFIX(my_worker_id)(), heap);
}

void* PREFIX(ver_separation_calloc)(size_t num, size_t size, unsigned heap)
{
  size_t sz = num*size;
  void*  ret = ver_separation_heap_alloc(sz, PREFIX(my_worker_id)(), heap);
  memset(ret, 0, sz);

  return ret;
}

void* PREFIX(ver_separation_realloc)(void* ptr, size_t size, unsigned heap)
{
  if (ptr && (size == 0))
  {
    // free. do nothing.
    return NULL;
  }

  void* ret = ver_separation_heap_alloc(size, PREFIX(my_worker_id)(), heap);

  if ( !ptr )
    return ret;

  size_t oldsize = (*separation_heap_size_map)[ptr];
  // copy data
  memcpy(ret, ptr, oldsize);
  // copy shadow
  memcpy((void*)GET_SHADOW_OF(ret), (void*)GET_SHADOW_OF(ptr), oldsize);

  separation_heap_size_map->erase(ptr);

  return ret;
}

void PREFIX(ver_separation_free)(void* ptr, unsigned heap)
{
  if (ptr) separation_heap_size_map->erase(ptr);
}

void PREFIX(push_separation_alloc_context)(uint32_t ctxt)
{
  if (invokedepth > 1) return;
  separation_alloc_context = ctxt;
}

void PREFIX(pop_separation_alloc_context)()
{
  if (invokedepth > 1) return;
  separation_alloc_context = 0;
}

uint32_t PREFIX(get_separation_alloc_context)()
{
  return separation_alloc_context;
}

}
