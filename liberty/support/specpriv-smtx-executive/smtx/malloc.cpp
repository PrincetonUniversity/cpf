#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <jemalloc/jemalloc.h>

#include <algorithm>

#include "api.h"
#include "internals/constants.h"
#include "internals/debug.h"
#include "internals/profile.h"
#include "internals/strategy.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/malloc.h"
#include "internals/smtx/protection.h"
#include "internals/smtx/smtx.h"

namespace specpriv_smtx
{

VerMallocChunk*  ver_malloc_chunks = NULL;
VerMallocBuffer* ver_malloc_buffer = NULL;

void ver_malloc_init()
{
  uint64_t begin = VER_MALLOC_CHUNK_BEGIN;
  uint64_t bound = VER_MALLOC_CHUNK_BOUND;
  uint64_t chunksize = VER_MALLOC_CHUNKSIZE;
  uint64_t num_chunks = (bound - begin) / chunksize;
  
  ver_malloc_chunks = (VerMallocChunk*)malloc( sizeof(VerMallocChunk) * MAX_WORKERS );
  ver_malloc_buffer = (VerMallocBuffer*)calloc( MAX_WORKERS, sizeof(VerMallocBuffer) );

  unsigned chunks_per_worker = num_chunks / MAX_WORKERS;
  unsigned rem = num_chunks % MAX_WORKERS;
  uint64_t n = VER_MALLOC_CHUNK_BEGIN;

  for (unsigned i = 0 ; i < MAX_WORKERS ; i++)
  {
    uint64_t sz = chunks_per_worker * chunksize;
    if (i < rem) sz += chunksize;

    ver_malloc_chunks[i].begin = ver_malloc_chunks[i].next = ver_malloc_chunks[i].nextchunk = n;
    ver_malloc_chunks[i].end = n + sz;

    n += sz;
  }
}

void ver_malloc_fini()
{
  for (unsigned i = 0 ; i < MAX_WORKERS ; i++)
  {
    size_t sz = ver_malloc_chunks[i].nextchunk - ver_malloc_chunks[i].begin;
    munmap( (void*)ver_malloc_chunks[i].begin, sz);
    munmap( (void*)GET_SHADOW_OF(ver_malloc_chunks[i].begin), sz);
  }

  free(ver_malloc_chunks);
}

static inline void insert_shadow_heap_pages(void* ptr, size_t size)
{
  uint8_t* page_shadow_begin = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  uint8_t* page_shadow_end = (uint8_t*)GET_SHADOW_OF( ((uint64_t)ptr+size-1) & PAGE_MASK );

  for ( ; page_shadow_begin <= page_shadow_end ; page_shadow_begin += PAGE_SIZE )
    shadow_heaps->insert( page_shadow_begin );
}

static inline void remove_shadow_heap_pages(void* ptr)
{
  // nothing can be done here, because there is no guarantee that the page with ptr is not used
  // anymore
}

void buffer_ver_malloc(unsigned wid, uint64_t ptr, uint32_t size, int32_t heap)
{
  VerMallocBuffer* buf = &(ver_malloc_buffer[wid]);

  if (buf->index == PAGE_SIZE)
  {
    broadcast_malloc_chunk(wid, (int8_t*)buf->elem, PAGE_SIZE * sizeof(VerMallocInstance));
    memset(buf, 0, sizeof(VerMallocBuffer));
    buf->index = 0;
  }

  VerMallocInstance* elem = &(buf->elem[buf->index++]);
  elem->ptr = ptr;
  elem->size = size;
  elem->heap = heap;
}

void* ver_malloc(unsigned wid, size_t size)
{
  uint64_t ret, next;

  ret = ver_malloc_chunks[wid].next;
  size = (size_t)( ROUND_UP( size, ALIGNMENT ) );
  next = ret + size;

  DBG("ver_malloc, wid %u begin %lx next %lx nextchunk %lx end %lx size %lx\n", wid,
      ver_malloc_chunks[wid].begin, ver_malloc_chunks[wid].next, ver_malloc_chunks[wid].nextchunk,
      ver_malloc_chunks[wid].end, size);

  DBG("ver_malloc, expected next: %lx\n", next);

  assert( next < ver_malloc_chunks[wid].end );

  while (ver_malloc_chunks[wid].nextchunk < next)
  {
    DBG("ver_malloc, try allocation, %p\n", (void*)ver_malloc_chunks[wid].nextchunk);

    int      prot = PROT_WRITE | PROT_READ;
    int      flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    unsigned chunksize = VER_MALLOC_CHUNKSIZE;

    void* mem = mmap( (void*)ver_malloc_chunks[wid].nextchunk, chunksize, prot, flags, -1, 0);

    DBG("ver_malloc, allocated, %p\n", mem);

    if ( mem == MAP_FAILED )  
      return NULL;
    
    void* shadow = mmap( (void*)GET_SHADOW_OF(mem), chunksize, prot, flags, -1, 0);
    
    if (shadow == MAP_FAILED) 
    {
      munmap(mem, chunksize);
      return NULL;
    }

    DBG("ver_malloc, shadow allocated, %p\n", mem);

    ver_malloc_chunks[wid].nextchunk += chunksize;
  }

  ver_malloc_chunks[wid].next = next;

  // notify to following workers
#if PROFILE
  uint64_t begin = rdtsc();
#endif

  buffer_ver_malloc( wid, ret, size, -1 );

  uint8_t* page = (uint8_t*)(ret & PAGE_MASK);
  uint8_t* shadow = (uint8_t*)(GET_SHADOW_OF(page));
  *shadow |= (0x80);

  // PROFDUMP("ver_alloc page %p\n", page);

  return (void*) ret;
}

void* update_ver_malloc(unsigned wid, size_t size, void* ptr)
{
  uint64_t ret, next;

  ret = ver_malloc_chunks[wid].next;
  size = (size_t)( ROUND_UP( size, ALIGNMENT ) );
  next = ret + size;

  DBG("update_ver_malloc at wid %u, ptr: %p ret: %p\n", wid, ptr, (void*)ret);

  if ((void*)ret != ptr)
  {
    DBG("update_ver_malloc, pointer mismatch: coming %p expected %lx\n", ptr, ret);
    assert(false); // TODO: this is a misspec
  }

  assert( next < ver_malloc_chunks[wid].end );
  
  if (ver_malloc_chunks[wid].nextchunk < next)
  {
    int      prot = PROT_WRITE | PROT_READ;
    int      flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    unsigned chunksize = VER_MALLOC_CHUNKSIZE;

    void* mem = mmap( (void*)ver_malloc_chunks[wid].nextchunk, chunksize, prot, flags, -1, 0);

    if ( mem == MAP_FAILED )  
    {
      DBG("update_ver_malloc failed to allocate the next chunk\n");
      assert(false); // TODO: this is a misspec
    }

    void* shadow = mmap( (void*)GET_SHADOW_OF(mem), chunksize, prot, flags, -1, 0);

    if (shadow == MAP_FAILED) 
    {
      DBG("update_ver_malloc failed to allocate a shadow for the next chunk\n");
      munmap(mem, chunksize);
      assert(false); // TODO: this is a misspec
    }

    ver_malloc_chunks[wid].nextchunk += chunksize;
  }

  ver_malloc_chunks[wid].next = next;

  insert_shadow_heap_pages((void*)ret, size);

  return (void*)ret;
}

void ver_free(unsigned wid, void* ptr)
{
  // notify to following workers

  broadcast_event( wid, ptr, 0, NULL, WRITE, FREE );
}

void update_ver_free(unsigned wid, void* ptr)
{
  // do nothing
}

/*
 * APIs
 */

static bool is_in_special_region(void* ptr)
{
  if ( ptr >= (char*)VER_MALLOC_CHUNK_BEGIN && ptr < (char*)VER_MALLOC_CHUNK_BOUND )
    return true;
  if ( ptr >= (char*)SEPARATION_HEAP_BEGIN && ptr < (char*)SEPARATION_HEAP_BOUND )
    return true;
  if ( ptr >= (char*)VER_SEPARATION_HEAP_BEGIN && ptr < (char*)VER_SEPARATION_HEAP_BOUND )
    return true;
  return false;
}

void* PREFIX(ver_malloc)(size_t size)
{
  Wid   wid = PREFIX(my_worker_id)();
  void* ret = ver_malloc(wid, size);
  DBG("ver_malloc, wid: %u, size: %lu\n", wid, size);

  PREFIX(ver_write)((int8_t*)ret, (uint32_t)size);
  (*heap_size_map)[ret] = size;
  insert_shadow_heap_pages(ret, size);

  return ret;
}

void* PREFIX(ver_calloc)(size_t num, size_t size)
{
  Wid   wid = PREFIX(my_worker_id)();
  void* ret = ver_malloc(wid, num*size);
  DBG("ver_calloc, wid: %u, size: %lu\n", wid, num*size);

  PREFIX(ver_write)((int8_t*)ret, (uint32_t)(num*size));
  (*heap_size_map)[ret] = num*size;
  insert_shadow_heap_pages(ret, num*size);

  return ret;
}

void* PREFIX(ver_realloc)(void* ptr, size_t size)
{
  Wid   wid = PREFIX(my_worker_id)();
  void* ret = ver_malloc(wid, size);
  DBG("ver_realloc, wid: %u, size: %lu\n", wid, size);

  if ( ptr != NULL && ptr != ret )
  {
    assert( heap_size_map->count(ptr) );
    size_t oldsize = (*heap_size_map)[ptr];

    PREFIX(ver_read)((int8_t*)ptr, (uint32_t)oldsize);
    memcpy(ret, ptr, oldsize);
    remove_shadow_heap_pages(ptr);
    heap_size_map->erase(ptr);
  }

  PREFIX(ver_write)((int8_t*)ret, (uint32_t)size);
  (*heap_size_map)[ret] = size;
  insert_shadow_heap_pages(ret, size);

  return ret;
}

void  PREFIX(ver_free)(void* ptr)
{
  if (ptr)
  {
    remove_shadow_heap_pages(ptr);
    heap_size_map->erase(ptr);
  }
  
  if ( is_in_special_region(ptr) )
  {
    // do nothing
  }
  else
  {
    shadow_free(ptr);
  }
}

void* PREFIX(malloc)(size_t size)
{
  void* ret = shadow_malloc(size);

  (*heap_size_map)[ret] = size;
  insert_shadow_heap_pages(ret, size);

  // PROFDUMP("alloc page %p\n", (void*)((uint64_t)(ret) & PAGE_MASK));

  return ret;
}

void* PREFIX(calloc)(size_t num, size_t size)
{
  void* ret = shadow_calloc(num, size);

  (*heap_size_map)[ret] = num*size;
  insert_shadow_heap_pages(ret, num*size);

  // PROFDUMP("alloc page %p\n", (void*)((uint64_t)(ret) & PAGE_MASK));

  return ret;
}

void* PREFIX(realloc)(void* ptr, size_t size)
{
  void* ret = shadow_realloc(ptr, size);

  if ( ptr != NULL && ptr != ret )
  {
    remove_shadow_heap_pages(ptr);
    heap_size_map->erase(ptr);
  }

  (*heap_size_map)[ret] = size;
  insert_shadow_heap_pages(ret, size);

  // PROFDUMP("alloc page %p\n", (void*)((uint64_t)(ret) & PAGE_MASK));

  return ret;
}

void  PREFIX(free)(void* ptr)
{
  if (ptr)
  {
    remove_shadow_heap_pages(ptr);
    heap_size_map->erase(ptr);
  }
  
  if ( is_in_special_region(ptr) )
  {
    // do nothing
  }
  else
  {
    shadow_free(ptr);
  }
}

}
