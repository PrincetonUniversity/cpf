#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#include <assert.h>

#include "heap.h"
#include "constants.h"
#include "config.h"

void mapped_heap_init(MappedHeap *mh)
{
  mh->heap = 0;
}

void heap_init(Heap *h, const char *desc, uint64_t len, void *forceAddress, uint64_t nonce)
{
  h->size = len;
  h->base = forceAddress;

#if PHYSICAL_PAGE_METHOD == PP_SHM
  snprintf(h->name,HNMAX, "/specpriv-%d-%lx-%ld-%s", getpid(), (uint64_t)forceAddress, nonce, desc);
  const int fd = shm_open(h->name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);

#elif PHYSICAL_PAGE_METHOD == PP_SPARSE_FILE
  snprintf(h->name,HNMAX, "/tmp/specpriv-%d-%lx-%ld-%s", getpid(), (uint64_t)forceAddress, nonce, desc);
  const int fd = open(h->name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);

#else
  #error "Bad PHYSICAL_PAGE_METHOD"
#endif

  if( fd < 0 )
  {
    perror("can't create shm");
    exit(0);
  }

  ftruncate(fd, len);

  close( fd );
}

static void heap_adjust_next(MappedHeap *mh)
{
  mh->next = (uint64_t*) mh->base;
}

void heap_fini(Heap *h)
{
#if PHYSICAL_PAGE_METHOD == PP_SHM
  shm_unlink(h->name);
#elif PHYSICAL_PAGE_METHOD == PP_SPARSE_FILE
  unlink(h->name);
#else
  #error "Bad PHYSICAL_PAGE_METHOD"
#endif
}

int heap_fd(Heap *h)
{

#if PHYSICAL_PAGE_METHOD == PP_SHM
  const int fd = shm_open(h->name, O_RDWR, S_IRUSR | S_IWUSR);
#elif PHYSICAL_PAGE_METHOD == PP_SPARSE_FILE
  const int fd = open(h->name, O_RDWR, S_IRUSR | S_IWUSR);
#else
  #error "Bad PHYSICAL_PAGE_METHOD"
#endif

  if( fd < 0 )
  {
    perror("can't reopen shm");
    exit(0);
  }

  return fd;
}

static void *map_at_with(void *base, uint64_t size, int protection, int flags, int fd)
{
  const uint64_t shsize = size / NUM_SUBHEAPS;

  void *zeroth_base = 0;
  char *shbase = (char*) base;
  uint64_t file_offset = 0;
  for(unsigned sh=0; sh<NUM_SUBHEAPS; ++sh)
  {
    char *effective_base = (char*) mmap( (void*) shbase, shsize, protection, flags, fd, file_offset );
    if( MAP_FAILED == shbase )
    {
      perror("mmap failed");
      _exit(0);
    }
    else if( shbase != 0 && effective_base != shbase )
    {
      perror("mmap gave a crappy address");
      _exit(0);
    }

    if( zeroth_base == 0 )
      zeroth_base = effective_base;

    shbase = effective_base + (1ULL << SUBHEAP_BITS);
    file_offset += shsize;
  }

  return zeroth_base;
}

void heap_map_shared(Heap *h, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" shared.\n", h->name));
  int fd = heap_fd(h);

  int flags = MAP_NORESERVE | MAP_SHARED;
  if( h->base )
    flags |= MAP_FIXED;

  mh->size = h->size;
  mh->base = map_at_with(h->base, mh->size, PROT_READ|PROT_WRITE, flags, fd);

  close(fd);

  heap_adjust_next(mh);
}

void heap_map_nrnw(Heap *h, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" shared.\n", h->name));
  int fd = heap_fd(h);

  int flags = MAP_NORESERVE | MAP_SHARED;
  if( h->base )
    flags |= MAP_FIXED;

  mh->size = h->size;
  mh->base = map_at_with(h->base, mh->size, 0, flags, fd);

  close(fd);

  heap_adjust_next(mh);
}

void heap_map_readonly(Heap *h, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" shared.\n", h->name));
  int fd = heap_fd(h);

  int flags = MAP_NORESERVE | MAP_SHARED;
  if( h->base )
    flags |= MAP_FIXED;

  mh->size = h->size;

  mh->base = map_at_with(h->base, mh->size, PROT_READ, flags, fd);

  close(fd);

  heap_adjust_next(mh);
}
void heap_map_anywhere(Heap *h, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" anywhere.\n", h->name));

  int fd = heap_fd(h);

  mh->size = h->size;
  mh->base = map_at_with(0, mh->size, PROT_READ|PROT_WRITE, MAP_NORESERVE|MAP_SHARED, fd);

  close(fd);
  heap_adjust_next(mh);

  DEBUG(printf(" ==> mapped to 0x%lx\n", (uint64_t) mh->base));
}

void heap_map_cow(Heap *h, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" copy-on-write.\n", h->name));

  int fd = heap_fd(h);

  int flags = MAP_NORESERVE | MAP_PRIVATE;
  if( h->base )
    flags |= MAP_FIXED;

  mh->size = h->size;
  mh->base = map_at_with(h->base, mh->size, PROT_READ|PROT_WRITE, flags, fd);
  heap_adjust_next(mh);

  close(fd);
}

void heap_map_anon(uint64_t len, void *forceAddress, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = 0;

  DEBUG(printf("Mapping heap anonymous heap\n"));

  int flags = MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS;
  if( forceAddress )
    flags |= MAP_FIXED;

  mh->size = len;
  mh->base = map_at_with(forceAddress, mh->size, PROT_READ|PROT_WRITE, flags, 0);
  heap_adjust_next(mh);

//  printf("Mapped anon to %ld\n", (uint64_t)forceAddress);
}


void heap_unmap(MappedHeap *mh)
{
  DEBUG(printf("Un-mapping heap \"%s\".\n", mh->heap ? mh->heap->name : "<anonymous>" ));

  for(unsigned i=0; i<NUM_SUBHEAPS; ++i)
    mh->next[i] = 0;
  mh->next = 0;
  mh->heap = 0;

  const uint64_t subheap_size = HEAP_SIZE / NUM_SUBHEAPS;
  for(unsigned i=0; i<NUM_SUBHEAPS; ++i)
  {
    char *subheap_base = (char*) mh->base;
    munmap( (void*) subheap_base, subheap_size );
    subheap_base += subheap_size;
  }
  mh->base = 0;
  mh->size = 0;
}

void *heap_alloc(MappedHeap *mh, uint64_t sz)
{
  return heap_alloc_subheap(mh,sz,0);
}

void *heap_alloc_subheap(MappedHeap *mh, uint64_t sz, SubHeap subheap)
{
  if( subheap >= NUM_SUBHEAPS )
    subheap = 0;

  void *p = (void*) &((uint8_t*)mh->base)[mh->next[subheap] ];

  // round sz up to a multiple of 16
	sz = ROUND_UP(sz,ALIGNMENT);

  mh->next[subheap] += sz;
  return p;
}

void heap_free(MappedHeap *mh, void *ptr)
{
  // pfft
}

uint64_t subheap_used(MappedHeap *mh, SubHeap subheap)
{
  return mh->next[subheap];
}

uint64_t heap_used(MappedHeap *mh)
{
  uint64_t sum = 0;
  for(SubHeap i=0; i<NUM_SUBHEAPS; ++i)
    sum += subheap_used(mh,i);

  return sum;
}

void heap_reset(MappedHeap *mh)
{
  // We store our array of 'next' pointers
  // at the beginning of the heap.  This makes
  // it easier for workers to re-map the heap
  // while maintaining their 'next' pointers.

  heap_adjust_next(mh);
  for(SubHeap i=0; i<NUM_SUBHEAPS; ++i)
    mh->next[i] = ( (uint64_t) i) << SUBHEAP_BITS;

  // Allocate space in the 0-th subheap to hold our
  // 'next' table.
  mh->next[0] += sizeof( uint64_t[NUM_SUBHEAPS] );
}

SubHeap find_subheap_in_pointer(void *ptr)
{
  uint64_t bits = (uint64_t)ptr;
  return (SubHeap) ( (bits & SUBHEAP_MASK) >> SUBHEAP_BITS );
}


void *subheap_base(void *base, SubHeap subheap)
{
  uint8_t *cbase =  (uint8_t*)base;
  const uint64_t offset = ((uint64_t)subheap) << SUBHEAP_BITS;
  uint8_t *shbase = cbase + offset;
  return (void*)shbase;
}

void *heap_translate(void *ptr, MappedHeap *mapped)
{
  Heap *heap = mapped->heap;
  assert( heap && "Not mapped");

  return (void*)
    ( ((uint64_t)ptr) - ((uint64_t) heap->base) + ((uint64_t) mapped->base) );
}


void *heap_inv_translate(void *ptr, MappedHeap *mapped)
{
  Heap *heap = mapped->heap;
  assert( heap && "Not mapped");

  return (void*)
    ( ((uint64_t)ptr) - ((uint64_t) mapped->base) + ((uint64_t) heap->base) );
}

uint64_t virtual_address_offset_to_shm_offset(uint64_t virtual_address_offset)
{
  // Input: an offset from the base address of a heap
  // Output: an offset within the shm file.

  const uint64_t subheap = (virtual_address_offset >> SUBHEAP_BITS);
  const uint64_t offset_within_subheap = virtual_address_offset & ((1ULL<<SUBHEAP_BITS)-1);
  const uint64_t subheap_size = HEAP_SIZE / NUM_SUBHEAPS;

  return offset_within_subheap + subheap * subheap_size;
}

uint64_t shm_offset_to_virtual_address_offset(uint64_t shm_offset)
{
  const uint64_t subheap_size = HEAP_SIZE / NUM_SUBHEAPS;
  const uint64_t subheap = shm_offset / subheap_size;
  const uint64_t offset_within_subheap = shm_offset - subheap * subheap_size;

  return offset_within_subheap + (subheap << SUBHEAP_BITS);
}

