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

  snprintf(h->name,HNMAX, "/specpriv-%d-%lx-%ld-%s", getpid(), (uint64_t)forceAddress, nonce, desc);

  const int fd = shm_open(h->name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if( fd < 0 )
  {
    perror("can't create shm");
    exit(0);
  }

  ftruncate(fd, len);

  close( fd );
}

void heap_fini(Heap *h)
{
  shm_unlink(h->name);
}

void heap_map_shared(Heap *h, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" shared.\n", h->name));

  const int fd = shm_open(h->name, O_RDWR, S_IRUSR | S_IWUSR);
  if( fd < 0 )
  {
    perror("can't reopen shm");
    exit(0);
  }

  int flags = MAP_NORESERVE | MAP_SHARED;
  if( h->base )
    flags |= MAP_FIXED;

  mh->size = h->size;
  mh->next = mh->base = mmap(h->base, h->size, PROT_READ|PROT_WRITE, flags, fd, 0);
  if( mh->base == MAP_FAILED )
  {
    perror("mmap failed for map-shared");
    exit(0);
  }

  close(fd);
}

void heap_map_read_only( Heap *h, MappedHeap *mh )
{
  assert( mh->heap == 0 && "Already mapped!" );
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" read-only.\n", h->name));

  // open with read-only permissions
  const int fd = shm_open( h->name, O_RDONLY, S_IRUSR );
  if( fd < 0 )
  {
    perror("can't reopen shm");
    exit(0);
  }

  int flags = MAP_NORESERVE | MAP_SHARED;
  if( h->base )
    flags |= MAP_FIXED;

  mh->size = h->size;
  mh->next = mh->base = mmap(h->base, h->size, PROT_READ, flags, fd, 0);
  if( mh->base == MAP_FAILED )
  {
    perror("mmap failed for map-read-only");
    exit(0);
  }

  close(fd);
}

void heap_map_anywhere(Heap *h, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" anywhere.\n", h->name));

  const int fd = shm_open(h->name, O_RDWR, S_IRUSR | S_IWUSR);
  if( fd < 0 )
  {
    perror("can't reopen shm");
    exit(0);
  }

  mh->size = h->size;
  mh->next = mh->base = mmap(0, h->size, PROT_READ|PROT_WRITE, MAP_NORESERVE|MAP_SHARED, fd, 0);
  if( mh->base == MAP_FAILED )
  {
    perror("mmap failed for map-anywhere");
    exit(0);
  }

  close(fd);

  DEBUG(printf(" ==> mapped to 0x%lx\n", (uint64_t) mh->base));
}

void heap_map_cow(Heap *h, MappedHeap *mh)
{
  assert( mh->heap == 0 && "Already mapped!");
  mh->heap = h;

  DEBUG(printf("Mapping heap \"%s\" copy-on-write.\n", h->name));

  const int fd = shm_open(h->name, O_RDWR, S_IRUSR | S_IWUSR);
  assert( fd >= 0 );

  int flags = MAP_NORESERVE | MAP_PRIVATE;
  if( h->base )
    flags |= MAP_FIXED;

  mh->size = h->size;
  mh->next = mh->base = mmap(h->base, h->size, PROT_READ|PROT_WRITE, flags, fd, 0);
  if( mh->base == MAP_FAILED )
  {
    perror("mmap failed for map-cow");
    exit(0);
  }

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
  mh->next = mh->base = mmap(forceAddress, len, PROT_READ|PROT_WRITE, flags, 0, 0);
  if( mh->base == MAP_FAILED )
  {
    perror("mmap failed for map-anon");
    exit(0);
  }

//  printf("Mapped anon to %ld\n", (uint64_t)forceAddress);
}


void heap_unmap(MappedHeap *mh)
{
  DEBUG(printf("Un-mapping heap \"%s\".\n", mh->heap ? mh->heap->name : "<anonymous>" ));

  mh->next = 0;
  mh->heap = 0;
  munmap( mh->base, mh->size );
}

void *heap_alloc(MappedHeap *mh, uint64_t sz)
{
  void *p = mh->next;

  // round sz up to a multiple of 16
	sz = ROUND_UP(sz,ALIGNMENT);

  mh->next = (void*) ( sz + (char*)mh->next );
  return p;
}

void heap_free(MappedHeap *mh, void *ptr)
{
  // pfft
}

uint64_t heap_used(MappedHeap *mh)
{
	return ((char*)mh->next) - (char*)mh->base;
}

void heap_reset(MappedHeap *mh)
{
  mh->next = mh->base;
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


