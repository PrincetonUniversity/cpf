#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

// Round-up/-down to a power of two    
#define ROUND_DOWN(n,k)   ( (~((k)-1)) & (uint64_t) (n) )    
#define ROUND_UP(n,k)     ROUND_DOWN( (n) + ((k) - 1), (k))    
    
// Align all allocation units to a multiple of this.    
// MUST BE A POWER OF TWO    
#define ALIGNMENT         (16)  

// GET SIZE OF THE OBJECT
#define GET_SIZE(ptr) *( (size_t*)( (uint64_t)(ptr) - sizeof(size_t) ) )
#define RESET_SIZE(ptr, sz) ( *( (size_t*)( (uint64_t)(ptr) - sizeof(size_t) ) ) = (sz) )

namespace slamp
{

const static size_t unit_sz = 0x100000000L;
static size_t unit_mask;
static uint64_t heap_begin;
static uint64_t heap_end;
static uint64_t heap_next;
static size_t pagesize;
static size_t pagemask;

/* make bound_malloc to return the address from the next page */

// preallocate a huge heap
void init_bound_malloc(void* heap_bound)
{
  unit_mask = ~(unit_sz-1);

  uint64_t a = reinterpret_cast<uint64_t>(sbrk(0));
  heap_begin = ( a + (unit_sz-1) ) & unit_mask;
  heap_end = reinterpret_cast<uint64_t>(heap_bound);

  for (uint64_t addr = heap_begin ; addr < heap_end ; addr += unit_sz)
  {
    void* p = mmap(reinterpret_cast<void*>(addr), unit_sz, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if ( p == MAP_FAILED)
    {
      //fprintf(stderr, "mmap failed with %p\n", (void*)addr);
      //perror("mmap failed for bound_malloc_init\n");
      //exit(0);
      heap_end = addr;
      break;
    }
  }

  heap_next = heap_begin;

  // remember pagesize to support bound_discard_page()
  pagesize = getpagesize();
  pagemask = ~(pagesize-1);

  //fprintf(stderr, "init_bound_malloc %lx to %lx\n", heap_begin, heap_end);
}

void fini_bound_malloc()
{
  for (uint64_t addr = heap_begin ; addr < heap_end ; addr += unit_sz)
    munmap( (void*)addr, unit_sz );
}

size_t get_object_size(void* ptr)
{
  return GET_SIZE(ptr);
}

void* bound_malloc(size_t size)
{
  // allocation unit size
  size_t sz = ROUND_UP(size, ALIGNMENT);  

  // store size of the unit
  size_t* sz_ptr = (size_t*)(heap_next);
  *sz_ptr = sz;
  heap_next += sizeof(size_t);

  void* ret = (size_t*)(heap_next);
  heap_next += sz;

  if (heap_next >= heap_end)
  {
    perror("Error: bound_malloc, not enough memory\n");
    exit(0);
  }

  return ret;
}

void bound_free(void* ptr)
{
  // do nothing for now
}

void* bound_calloc(size_t num, size_t size)
{
  void* ptr = bound_malloc(num*size);
  memset(ptr, '\0', num*size);
  return ptr;
}

void* bound_realloc(void* ptr, size_t size)
{
  if (ptr == NULL)
  {
    /*
       In case that ptr is a null pointer, the function behaves like malloc, assigning
       a new block of size bytes and returning a pointer to its beginning.
     */
    return bound_malloc(size);
  }

  if (size == 0)
  {
    /*
       If size is zero, the return value depends on the particular library
       implementation (it may or may not be a null pointer), but the returned
       pointer shall not be used to dereference an object in any case.
     */
    bound_free(ptr);
    return NULL;
  }

  size_t old_sz = GET_SIZE(ptr);
  if (old_sz >= size)
  {
    RESET_SIZE(ptr, size);
    return ptr;   
  }
  else
  { 
    // do the same thing as malloc
    size_t sz = ROUND_UP(size, ALIGNMENT);  

    size_t* sz_ptr = (size_t*)(heap_next);
    *sz_ptr = sz;
    heap_next += sizeof(size_t);

    void* new_ptr = (size_t*)(heap_next);
    heap_next += sz;

    // if there is not enough memory, just return the original pointer
    if (heap_next > heap_end)
    {
      heap_next -= sz;
      heap_next -= sizeof(size_t);
      return ptr;
    }

    memcpy(new_ptr, ptr, old_sz);
    return new_ptr;
  }
}

void bound_discard_page()
{
  heap_next = ( heap_next + (pagesize-1) ) & pagemask;
  if (heap_next >= heap_end)
  {
    perror("Error: bound_malloc, not enough memory\n");
    exit(0);
  }
}

} // namespace slamp
