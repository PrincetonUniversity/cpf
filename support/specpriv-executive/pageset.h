/* TODO:
 * - presently, this uses a big (simple) bit-vector.
 * - ideally, use a sorted interval list instead of a bit vector
 */
#ifndef LLVM_LIBERTY_SMTX2_PAGESET_H
#define LLVM_LIBERTY_SMTX2_PAGESET_H

#include <stdint.h>
#include "types.h"
#include "config.h"

typedef uint64_t PageNumber; // Integer number of pages from beginning of heap, i.e. 0, 1, 2, ...
typedef uint64_t PageLength; // Integer number of pages, i.e. 1, 2, 3, ...

struct s_page_interval
{
  PageNumber  low_inclusive;
  PageNumber  high_exclusive;
};
typedef struct s_page_interval PageInterval;

/*
typedef void * (*Reallocator)(void *old, Len size);

struct s_page_set
{
  PageBaseLength      *entries;
  Len                 capacity;
  Len                 size;

  Reallocator         realloc;
};
typedef struct s_page_set PageSet;
*/


/* Want this to be a compile-time constant
 * We will assert that our assumption matches the
 * dynamic value from getpagesize() in pageset_init()
 */
#define ASSUMED_PAGE_SIZE               (4*KB)

/* Size of a heap, in pages == size of bit vector, in bits*/
#define HEAP_SIZE_PAGES                 ( (HEAP_SIZE + ASSUMED_PAGE_SIZE - 1) / ASSUMED_PAGE_SIZE )

#define BITS_PER_WORD                   ( BITS_PER_BYTE * sizeof(uint64_t) )

#define PAGESET_BIT_VECTOR_SIZE_BITS    ( HEAP_SIZE_PAGES )

/* Size of bit vector, in 64-bit words */
#define PAGESET_BIT_VECTOR_SIZE_WORDS   ( (PAGESET_BIT_VECTOR_SIZE_BITS + BITS_PER_WORD - 1) / BITS_PER_WORD )

/* Size of bit vector, bytes */
#define PAGESET_BIT_VECTOR_SIZE_BYTES   ( sizeof(uint64_t) * PAGESET_BIT_VECTOR_SIZE_WORDS )


/* TODO: dumb, bit-vector implementation */
struct s_page_set
{
  uint64_t          bitvec[ PAGESET_BIT_VECTOR_SIZE_WORDS ];
};
typedef struct s_page_set PageSet;


void pageset_init(PageSet *pset);
void pageset_fini(PageSet *pset);

void pageset_add_page(PageSet *pset, PageNumber page);
Bool pageset_test_page(PageSet *pset, PageNumber page);

Bool pageset_next_interval(PageSet *pset, PageInterval *previous);

#endif



