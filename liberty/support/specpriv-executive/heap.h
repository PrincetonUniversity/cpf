#ifndef LIBERTY_SPECPRIV_EXECUTIVE_HEAP_H
#define LIBERTY_SPECPRIV_EXECUTIVE_HEAP_H

#include <stdint.h>

#define HNMAX   (256)

typedef struct s_heap Heap;
typedef struct s_mapped_heap MappedHeap;

struct s_heap
{
  // name for this heap
  char      name[HNMAX];

  // extents
  void    * base;
  uint64_t  size;
};

struct s_mapped_heap
{
  Heap    * heap;
  uint64_t  size;
  void    * base;
  void    * next;
};

// Create an onymous heap
void heap_init(Heap *h, const char *desc, uint64_t len, void *forceAddress, uint64_t nonce);
void heap_fini(Heap *h);

// Initialize MappedHeap structure
void mapped_heap_init(MappedHeap *mh);

// un/map those heaps
void heap_map_shared(Heap *h, MappedHeap *mh);
void heap_map_cow(Heap *h, MappedHeap *mh);
void heap_map_anon(uint64_t len, void *forceAddress, MappedHeap *mh);
void heap_unmap(MappedHeap *mh);
void heap_map_anywhere(Heap *h, MappedHeap *mh);

// allocate
void *heap_alloc(MappedHeap *h, uint64_t sz);
void heap_free(MappedHeap *h, void *ptr);
uint64_t heap_used(MappedHeap *h);
void heap_reset(MappedHeap *h);

// Translate pointers
// Given a pointer which expects a heap at its
// natural address, translate that pointer to
// account for the mapped address.
// If the mapped heap h is mapped at an address X
// instead of the natural address Y of the heap specified
// at heap creation, then return (ptr-Y)+X
void *heap_translate(void *ptr, MappedHeap *h);

// Inverse of the previous
void *heap_inv_translate(void *ptr, MappedHeap *h);


#endif

