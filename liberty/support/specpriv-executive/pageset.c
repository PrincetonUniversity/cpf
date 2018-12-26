#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "pageset.h"

void pageset_init(PageSet *pset)
{
  assert( ASSUMED_PAGE_SIZE == getpagesize() );
  memset( pset->bitvec, 0, PAGESET_BIT_VECTOR_SIZE_BYTES);
}

void pageset_fini(PageSet *pset)
{
}

void pageset_add_page(PageSet *pset, PageNumber page)
{
  const uint64_t word = page / BITS_PER_WORD;
  const uint64_t bit  = page % BITS_PER_WORD;
  const uint64_t mask = 1ULL << bit;

  pset->bitvec[ word ] |= mask;
}

Bool pageset_test_page(PageSet *pset, PageNumber page)
{
  const uint64_t word = page / BITS_PER_WORD;
  const uint64_t bit  = page % BITS_PER_WORD;
  const uint64_t mask = 1ULL << bit;

  return 0 != (pset->bitvec[word] & mask);
}

Bool pageset_next_interval(PageSet *pset, PageInterval *interval)
{
  PageNumber low = interval->high_exclusive;
  while( low<PAGESET_BIT_VECTOR_SIZE_BITS && ! pageset_test_page(pset, low) )
    ++low;

  if( low == PAGESET_BIT_VECTOR_SIZE_BITS )
    return 0;

  PageNumber high = low+1;
  while( high<PAGESET_BIT_VECTOR_SIZE_BITS && pageset_test_page(pset, high) )
    ++high;

  return 1;
}




