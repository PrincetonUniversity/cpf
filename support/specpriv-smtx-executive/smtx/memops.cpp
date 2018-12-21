#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "internals/debug.h"
#include "internals/constants.h"
#include "internals/profile.h"
#include "internals/smtx/smtx.h"

namespace specpriv_smtx
{

/*
 * A byte of metadata for each byte, but only 2 LSBs are used. 
 * Bit 2 is to indicate load, Bit 1 is to indicate store
 * and Bit 0 is to indicate load happened before store.
 * Bit 3 indicates that the address supposed to hold the loop invariant value
 */

void PREFIX(ver_read1)(int8_t* ptr)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  //uint8_t* page_shadow = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  //*page_shadow |= (0x80);

  uint8_t* shadow = (uint8_t*)GET_SHADOW_OF(ptr);
  uint8_t  access = *shadow;
  *shadow = (uint8_t)(access | 0x04);

#if MEMDBG
  DBG("read1 %p page %p val %lx shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, *ptr, *shadow);
#endif

#if PROFILE_MEMOPS
  r1[ PREFIX(my_worker_id)() ] += 1;
#endif
#endif
}

void PREFIX(ver_read2)(int8_t* ptr)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  //uint8_t* page_shadow = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  //*page_shadow |= (0x80);

  uint16_t* shadow = (uint16_t*)GET_SHADOW_OF(ptr);
  uint16_t  access = *shadow;
  *shadow = (uint16_t)(access | 0x0404);

#if MEMDBG
  DBG("read2 %p page %p val %lx shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, *(uint16_t*)ptr, *shadow);
#endif

#if PROFILE_MEMOPS
  r2[ PREFIX(my_worker_id)() ] += 2;
#endif
#endif
}

void PREFIX(ver_read4)(int8_t* ptr)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  //uint8_t* page_shadow = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  //*page_shadow |= (0x80);

  uint32_t* shadow = (uint32_t*)GET_SHADOW_OF(ptr);
  uint32_t  access = *shadow;
  *shadow = access | 0x04040404;

#if MEMDBG
  DBG("read4 %p page %p val %lx shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, *(uint32_t*)ptr, *shadow);
#endif

#if PROFILE_MEMOPS
  r4[ PREFIX(my_worker_id)() ] += 4;
#endif
#endif
}

void PREFIX(ver_read8)(int8_t* ptr)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  //uint8_t* page_shadow = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  //*page_shadow |= (0x80);

  uint64_t* shadow = (uint64_t*)GET_SHADOW_OF(ptr);
  uint64_t  access = *shadow;
  *shadow = (access | 0x0404040404040404L);

#if MEMDBG
  DBG("read8 %p page %p val %lx shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, *(uint64_t*)ptr, *shadow);
#endif


#if PROFILE_MEMOPS
  r8[ PREFIX(my_worker_id)() ] += 8;
#endif
#endif
}

void PREFIX(ver_read)(int8_t* ptr, uint32_t size)
{
  return;
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  /*
  uint8_t* page_shadow_begin = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  uint8_t* page_shadow_end = (uint8_t*)GET_SHADOW_OF( ((uint64_t)ptr+size-1) & PAGE_MASK );

  for ( ; page_shadow_begin <= page_shadow_end ; page_shadow_begin += PAGE_SIZE )
    *page_shadow_begin |= (uint8_t)0x80; 
  */

  uint32_t rem = size & 0x7;

  for (unsigned i = 0, z = size-rem ; i < z ; i+=8)
  {
#if 0
    uint64_t* shadow = (uint64_t*)GET_SHADOW_OF( &ptr[i] );
    uint64_t  access = *shadow;
    *shadow = (access | 0x0404040404040404L);
#else
    PREFIX(ver_read8)(&ptr[i]);
#endif
  }

  for (unsigned i = size-rem ; i < size ; i++)
  {
  #if 0
    uint8_t* shadow = (uint8_t*)GET_SHADOW_OF( &ptr[i] );
    uint8_t  access = *shadow;
    *shadow = (uint8_t)(access | 0x04);
  #else
    PREFIX(ver_read1)(&ptr[i]);
  #endif
  }
#endif
}

void PREFIX(ver_write1)(int8_t* ptr)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  //uint8_t* page_shadow = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  //*page_shadow |= (0x80);

  uint8_t* shadow = (uint8_t*)GET_SHADOW_OF(ptr);
  uint8_t  access = *shadow;

  if ((access & 0x02) == 0x02)
  {
#if MEMDBG
    DBG("write1 %p page %p shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, access);
#endif
    return;
  }

  uint8_t mask1 = (uint8_t)(access >> 2);
  uint8_t mask2 = (uint8_t)(~(access >> 1));
  uint8_t mask = mask1 & mask2 & 0x1;

  access = (access | 0x02 | mask);
  *shadow = access;

#if MEMDBG
  DBG("write1 %p page %p shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, access);
#endif

#if PROFILE_MEMOPS
  w1[ PREFIX(my_worker_id)() ] += 1;
#endif
#endif
}

void PREFIX(ver_write2)(int8_t* ptr)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  //uint8_t* page_shadow = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  //*page_shadow |= (0x80);

  uint16_t* shadow = (uint16_t*)GET_SHADOW_OF(ptr);
  uint16_t  access = *shadow;

  if ((access & 0x0202) == 0x0202)
  {
#if MEMDBG
    DBG("write2 %p page %p shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, access);
#endif
    return;
  }

  uint16_t mask1 = (uint16_t)(access >> 2);
  uint16_t mask2 = (uint16_t)(~(access >> 1));
  uint16_t mask = mask1 & mask2 & 0x0101;

  access = (access | 0x0202 | mask);
  *shadow = access;

#if MEMDBG
  DBG("write2 %p page %p shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, access);
#endif

#if PROFILE_MEMOPS
  w2[ PREFIX(my_worker_id)() ] += 2;
#endif
#endif
}

void PREFIX(ver_write4)(int8_t* ptr)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  //uint8_t* page_shadow = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  //*page_shadow |= (0x80);

  uint32_t* shadow = (uint32_t*)GET_SHADOW_OF(ptr);
  uint32_t  access = *shadow;

  if ((access & 0x02020202) == 0x02020202)
  {
  #if MEMDBG
    DBG("write4 %p page %p shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, access);
  #endif
    return;
  }

  uint32_t mask1 = access >> 2;
  uint32_t mask2 = ~(access >> 1);
  uint32_t mask = mask1 & mask2 & 0x01010101;

  access = (access | 0x02020202 | mask);
  *shadow = access;

#if MEMDBG
  DBG("write4 %p page %p shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, access);
#endif

#if PROFILE_MEMOPS
  w4[ PREFIX(my_worker_id)() ] += 4;
#endif
#endif
}

void PREFIX(ver_write8)(int8_t* ptr)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  //uint8_t* page_shadow = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  //*page_shadow |= (0x80);

  uint64_t* shadow = (uint64_t*)GET_SHADOW_OF(ptr);
  uint64_t  access = *shadow;

  if ((access & 0x0202020202020202L) == 0x0202020202020202L)
  {
  #if MEMDBG
    DBG("write8 %p page %p val %lx shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, *((uint64_t*)ptr), access);
  #endif
    return;
  }

  uint64_t mask1 = access >> 2;
  uint64_t mask2 = ~(access >> 1);
  uint64_t mask = mask1 & mask2 & 0x0101010101010101L;

  access = (access | 0x0202020202020202L | mask);
  *shadow = access;

#if MEMDBG
  DBG("write8 %p page %p val %lx shadow %lx\n", ptr, (size_t)ptr & PAGE_MASK, *((uint64_t*)ptr), access);
#endif

#if PROFILE_MEMOPS
  w8[ PREFIX(my_worker_id)() ] += 8;
#endif
#endif
}

void PREFIX(ver_write)(int8_t* ptr, uint32_t size)
{
#if 1
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= ptr && ptr < (int8_t*)stack_bound )
    return;
#endif

  /*
  uint8_t* page_shadow_begin = (uint8_t*)GET_SHADOW_OF( (uint64_t)ptr & PAGE_MASK );
  uint8_t* page_shadow_end = (uint8_t*)GET_SHADOW_OF( ((uint64_t)ptr+size-1) & PAGE_MASK );

  for ( ; page_shadow_begin <= page_shadow_end ; page_shadow_begin += PAGE_SIZE )
    *page_shadow_begin |= (uint8_t)0x80; 
  */

  uint32_t rem = size & 0x7;
  
  for (unsigned i = 0, z = size-rem ; i < z ; i+=8)
  {
  #if 0
    uint64_t* shadow = (uint64_t*)GET_SHADOW_OF( &ptr[i] );
    uint64_t  access = *shadow;

    if ((access & 0x0202020202020202L) == 0x0202020202020202L)
      continue;

    uint64_t mask1 = access >> 2;
    uint64_t mask2 = ~(access >> 1);
    uint64_t mask = mask1 & mask2 & 0x0101010101010101L;

    access = (access | 0x0202020202020202L | mask);
    *shadow = access;
  #else
    PREFIX(ver_write8)(&ptr[i]);
  #endif
  }

  for (unsigned i = size-rem ; i < size ; i++)
  {
  #if 0
    uint8_t* shadow = (uint8_t*)GET_SHADOW_OF( &ptr[i] );
    uint8_t  access = *shadow;

    if ((access & 0x02) == 0x02)
      continue;

    uint8_t mask1 = (uint8_t)(access >> 2);
    uint8_t mask2 = (uint8_t)(~(access >> 1));
    uint8_t mask = mask1 & mask2 & 0x1;

    access = (access | 0x02 | mask);
    *shadow = access;
  #else
    PREFIX(ver_write1)(&ptr[i]);
  #endif
  }
#endif
}

void PREFIX(ver_memmove)(int8_t* write_ptr, uint32_t size, int8_t* read_ptr)
{
#if !HANDLE_STACK
  if ( (int8_t*)stack_begin <= write_ptr && write_ptr < (int8_t*)stack_bound )
    return;
#endif

  PREFIX(ver_read)(read_ptr, size);
  PREFIX(ver_write)(write_ptr, size);
}

}
