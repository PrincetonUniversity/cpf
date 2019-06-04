#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "config.h"
#include "api.h"
#include "timer.h"
#include "private.h"
#include "checkpoint.h"
#include "fiveheaps.h"

// First iteration of this invocation.
// May be >0 after recovery from misspeculation.
static Iteration firstIteration;

// The checkpoint granularity:
// == The greatest multiple of numWorkers <= MIN_CHECKPOINT_GRANULARITY
static Iteration checkpointGranularity;

// Range [low,high) of bytes which have been defined in the shadow heap.
// (these addresses are relative to the natural position of the shadow heap)
static uint8_t *shadow_lowest_inclusive,
               *shadow_highest_exclusive;

// 8-,16-,32-, and 64-bit shadow memory codes to indicate
// the current iteration.
static uint8_t code8 = 0;
static uint16_t code16 = 0;
static uint32_t code32 = 0;
static uint64_t code64 = 0;

#if SHADOW_MEM == VECTOR
static __m128i code128;
#endif


//------------------------------------------------------------------
// Privatization support

static void __specpriv_reset_shadow_range(void)
{
  // Initially, an empty range has been touched.
  shadow_lowest_inclusive = (uint8_t*) (SHADOW_ADDR + (1UL<<POINTER_BITS));
  shadow_highest_exclusive = (uint8_t*) (SHADOW_ADDR);
}

static void update_shadow_range(uint8_t *shadow, uint64_t len)
{
  if (shadow < shadow_lowest_inclusive)
    shadow_lowest_inclusive = shadow;
  if (shadow_highest_exclusive < shadow + len)
    shadow_highest_exclusive = shadow + len;
}

// Called once at beginning of invocation, before workers are spawned.
void __specpriv_init_private(void)
{
  // Determine the checkpoint granularity.
  // round the MAX_CHECKPOINT_GRANULARITY down to a multiple
  // of numWorkers.
  const Wid numWorkers = __specpriv_num_workers();
  checkpointGranularity = MAX_CHECKPOINT_GRANULARITY - (MAX_CHECKPOINT_GRANULARITY % numWorkers);

  //DEBUG( checkpointGranularity = 10 );

  __specpriv_reset_shadow_range();
}

static void __specpriv_set_iter(Iteration i)
{
  // The metadata value which indicates a value defined during /this/ iteration.
  code8 = ( (uint8_t) ( ( (i-firstIteration) % checkpointGranularity) + NUM_RESERVED_SHADOW_VALUES ) );

  code16 =  code8 | (( (uint16_t)  code8 ) <<  8);
  code32 = code16 | (( (uint32_t) code16 ) << 16);
  code64 = code32 | (( (uint64_t) code32 ) << 32);

#if SHADOW_MEM == VECTOR
  const __m128i _code8  = _mm_cvtsi32_si128( code8 );
  const __m128i _code16 = _mm_or_si128( _mm_slli_si128( _code8, 1), _code8 );
  const __m128i _code32 = _mm_or_si128( _mm_slli_si128(_code16, 2), _code16 );
  const __m128i _code64 = _mm_or_si128( _mm_slli_si128(_code32, 4), _code32 );

  code128 = _mm_or_si128( _mm_slli_si128(_code64, 8), _code64 );
#endif
}

void __specpriv_set_first_iter(Iteration i)
{
  firstIteration = i;

  __specpriv_set_iter(i);
}

Iteration __specpriv_get_first_iter(void)
{
  return firstIteration;
}

// Called by __specpriv_worker_starts()
// Called by __specpriv_end_iter()
void __specpriv_advance_iter(Iteration i, uint32_t ckptUsed)
{
  __specpriv_set_iter(i);

  /*
  // cannot use __specpriv_get_ckpt_check unless we check if ckpt_check was
  performed for iteration i
  //
  if (__specpriv_get_ckpt_check())
    __specpriv_worker_perform_checkpoint(0);
  */

  if (ckptUsed == 0)
    return;

  if (__specpriv_runOnEveryIter()) {
    if (code8 == NUM_RESERVED_SHADOW_VALUES && i > 0)
      __specpriv_worker_perform_checkpoint(0);

    return;
  }

  const Wid numWorkers = __specpriv_num_workers();
  Iteration prevI = i - numWorkers;
  Iteration prevR = (prevI - firstIteration) / checkpointGranularity;
  Iteration curR = (i - firstIteration) / checkpointGranularity;

  //DEBUG(
  //    printf("PreI:%d, curI:%d, firstIteration:%d, checkpointGranularity:%d\n",
  //           prevI, i, firstIteration, checkpointGranularity));

  if (prevI >= firstIteration && prevR + 1 == curR )
    __specpriv_worker_perform_checkpoint(0);
}

// Update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
void __specpriv_private_write_range(void *ptr, uint64_t len)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() ) // shadow is only mapped in worker processes
  {
    uint8_t *shadow = (uint8_t*) ( SHADOW_ADDR | (uint64_t)ptr );
    // TODO: speed this up.
    if( memchr(shadow, READ_LIVE_IN, len) )
      __specpriv_misspec("misspec during private write range (1)");
    memset(shadow, code8, len);

    update_shadow_range(shadow, len);

    DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );

    TOUT(worker_private_bytes_written += len);
  }

  TADD(worker_time_in_priv_write,start);
}

// Does not update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
static inline void __specpriv_private_read_range_internal(uint8_t *start, uint8_t *stop, const char *name)
{
#if SHADOW_MEM == VECTOR
  uint8_t *lo4  = (uint8_t*) ROUND_UP( start, sizeof(uint32_t) );
  uint8_t *lo16 = (uint8_t*) ROUND_UP( start, sizeof(__m128i) );
  uint8_t *hi16 = (uint8_t*) ROUND_DOWN( stop, sizeof(__m128i) );
  uint8_t *hi4  = (uint8_t*) ROUND_DOWN( stop, sizeof(uint32_t) );

  // First check unaligned bytes
  for(uint8_t *i=start; i<lo4; ++i)
  {
    const uint8_t meta = *i;
    if( meta == LIVE_IN )
      *i = READ_LIVE_IN;
    else if( meta != code8 && meta != READ_LIVE_IN )
      __specpriv_misspec(name);
  }

  // Check 4-byte aligned words
  if( lo4 < hi4 )
  {
    for(uint32_t *i=(uint32_t*)lo4; i<(uint32_t*)lo16; ++i)
    {
      const uint32_t meta = *i;
      if( meta == V32(LIVE_IN) )
        *i = V32( READ_LIVE_IN );
      else if( meta != code32 && meta != V32(READ_LIVE_IN) )
        __specpriv_misspec(name);
    }

    // Check the 16-byte aligned vectors in the middle
    // (hopefully the bulk of the operation)
    if( lo16 < hi16 )
    {
      const __m128i zero = _mm_setzero_si128();
      const __m128i _code128 = _mm_load_si128(&code128);

      for(__m128i *i=(__m128i*)lo16; i<(__m128i*)hi16; ++i)
      {
        const __m128i meta = _mm_load_si128(i);

        const __m128i cmp_code = (__m128i) _mm_cmpeq_epi8(meta, _code128);
        const uint16_t eq_code = _mm_movemask_epi8(cmp_code);

        if( eq_code != 0xffffu )
        {
          const __m128i cmp_0 = (__m128i) _mm_cmpeq_epi8(meta, zero);
          const uint16_t eq_zero = _mm_movemask_epi8(cmp_0);
TODO: make sure that bytes marked LIVE_IN become READ_LIVE_IN
          if( (eq_zero | eq_code) != 0xffffu )
            __specpriv_misspec(name);
        }
      }
    }

    for(uint32_t *i=(uint32_t*)hi16; i<(uint32_t*)hi4; ++i)
    {
      const uint32_t meta = *i;
      if( meta == V32(LIVE_IN) )
        *i = V32(READ_LIVE_IN);
      else if( meta != code32 && meta != V32(READ_LIVE_IN) )
        __specpriv_misspec(name);
    }
  }

  // Finally, check unaligned bytes
  for(uint8_t *i=hi4; i<stop; ++i)
  {
    const uint8_t meta = *i;
    if( meta == LIVE_IN )
      *i = READ_LIVE_IN;
    else if( meta != code8 && meta != READ_LIVE_IN )
      __specpriv_misspec(name);
  }
#endif

#if SHADOW_MEM == NATIVE
  uint8_t *lo8  = (uint8_t*) ROUND_UP( start, sizeof(uint64_t) );
  uint8_t *hi8  = (uint8_t*) ROUND_DOWN( stop, sizeof(uint64_t) );

  // First check unaligned bytes
  for(uint8_t *i=start; i<lo8; ++i)
  {
    const uint8_t meta = *i;
    if( meta == LIVE_IN )
      *i = READ_LIVE_IN;
    else if( meta != code8 && meta != READ_LIVE_IN )
      __specpriv_misspec(name);
  }

  for(uint64_t *i=(uint64_t*)lo8; i<(uint64_t*)hi8; ++i)
  {
    const uint64_t meta = *i;
    if( meta == V64(LIVE_IN) )
      *i = V64(READ_LIVE_IN);
    else if( meta != code64 && meta != V64(READ_LIVE_IN) )
      __specpriv_misspec(name);
  }


  // Finally, check unaligned bytes
  for(uint8_t *i=hi8; i<stop; ++i)
  {
    const uint8_t meta = *i;
    if( meta == LIVE_IN )
      *i = READ_LIVE_IN;
    else if( meta != code8 && meta != READ_LIVE_IN )
      __specpriv_misspec(name);
  }
#endif
}

// Update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
void __specpriv_private_read_range(void *ptr, uint64_t len, const char *name)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() ) // shadow is only mapped in worker processes
  {
    uint8_t *shadow = (uint8_t*) ( SHADOW_ADDR | (uint64_t)ptr );

    __specpriv_private_read_range_internal(shadow, &shadow[len], name);

    if( shadow < shadow_lowest_inclusive )
      shadow_lowest_inclusive = shadow;
    if( shadow_highest_exclusive < shadow + len )
      shadow_highest_exclusive = shadow + len;

    DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );

    TOUT(worker_private_bytes_read += len);
  }

  TADD(worker_time_in_priv_read,start);
}

// The remainder of the file contains
// specialized versions of private_write and private_read
// for small ranges.

// For 1-byte loads/stores

void __specpriv_private_write_1b(void *ptr)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    uint8_t *shadow = (uint8_t*) ( SHADOW_ADDR | (uint64_t)ptr );
    const uint8_t meta = *shadow;
    if( meta == READ_LIVE_IN )
      __specpriv_misspec("misspec during private write 1b");
    *shadow = code8;

    update_shadow_range(shadow, 1);

    TOUT(worker_private_bytes_written += 1);
  }

  TADD(worker_time_in_priv_write,start);
}

void __specpriv_private_read_1b(void *ptr, const char *name)
{
  // TODO optimize
  __specpriv_private_read_range(ptr,1, name);
}

// For 2-byte loads/stores

void __specpriv_private_write_2b(void *ptr)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    uint16_t *shadow = (uint16_t*) ( SHADOW_ADDR | (uint64_t)ptr );
    const uint16_t meta = *shadow;
    if( ( (meta>>8) & 0x0ff ) == READ_LIVE_IN )
      __specpriv_misspec("misspec during private write 2b");
    if( ( meta & 0x0ff ) == READ_LIVE_IN )
      __specpriv_misspec("misspec during private write 2b");
    *shadow = code16;

    update_shadow_range((uint8_t*)shadow,2);

    DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );

    TOUT(worker_private_bytes_written += 2);
  }

  TADD(worker_time_in_priv_write,start);
}

void __specpriv_private_read_2b(void *ptr, const char *name)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    uint16_t *shadow = (uint16_t*) ( SHADOW_ADDR | (uint64_t)ptr );
    const uint16_t meta = *shadow;

    TOUT(worker_private_bytes_read += 2);

    update_shadow_range((uint8_t*)shadow,2);

    if( meta == code16 )
    {
      TADD(worker_time_in_priv_read,start);
      return;
    }

    else if( meta == V16( LIVE_IN ) )
    {
      *shadow = V16( READ_LIVE_IN );
      TADD(worker_time_in_priv_read,start);
      return;
    }

    else if( meta == V16( READ_LIVE_IN ) )
    {
      TADD(worker_time_in_priv_read,start);
      return;
    }

    // At this point, it's very likely that we
    // will misspeculate...  unless we read
    // 4 bytes and some were defined in this iteration
    // and the rest were live-in.

    const uint8_t m0 = (meta >> 0) & 0xff;
    uint8_t *bs = (uint8_t*) shadow;
    if( m0 == LIVE_IN )
      bs[0] = READ_LIVE_IN;
    else if( m0 != code8 && m0 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m1 = (meta >> 8) & 0xff;
    if( m1 == LIVE_IN )
      bs[1] = READ_LIVE_IN;
    else if( m1 != code8 && m1 != READ_LIVE_IN )
      __specpriv_misspec(name);
  }

  TADD(worker_time_in_priv_read,start);

}

// For 4-byte loads/stores

// Update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
void __specpriv_private_write_4b(void *ptr)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    uint32_t *shadow = (uint32_t*) ( SHADOW_ADDR | (uint64_t)ptr );
    // TODO: optimize
    if( memchr(shadow, READ_LIVE_IN, sizeof(uint32_t) ) )
      __specpriv_misspec("misspec during private write 4b");
    *shadow = code32;

    update_shadow_range((uint8_t*)shadow,4);

    DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );

    TOUT(worker_private_bytes_written += 4);
  }

  TADD(worker_time_in_priv_write,start);
}

// Update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
void __specpriv_private_read_4b(void *ptr, const char *name)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    uint32_t *shadow = (uint32_t*) ( SHADOW_ADDR | (uint64_t)ptr );
    const uint32_t meta = *shadow;

    TOUT(worker_private_bytes_read += 4);

    update_shadow_range((uint8_t*)shadow, 4);

    DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );

    if( meta == code32 )
    {
      TADD(worker_time_in_priv_read,start);
      return;
    }

    else if( meta == V32( LIVE_IN ) )
    {
      *shadow = V32( READ_LIVE_IN );
      TADD(worker_time_in_priv_read,start);
      return;
    }

    else if( meta == V32( READ_LIVE_IN ) )
    {
      TADD(worker_time_in_priv_read,start);
      return;
    }

    // At this point, it's very likely that we
    // will misspeculate...  unless we read
    // 4 bytes and some were defined in this iteration
    // and the rest were live-in.

    const uint8_t m0 = (meta >> 0) & 0xff;
    uint8_t *bs = (uint8_t*) shadow;
    if( m0 == LIVE_IN )
      bs[0] = READ_LIVE_IN;
    else if( m0 != code8 && m0 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m1 = (meta >> 8) & 0xff;
    if( m1 == LIVE_IN )
      bs[1] = READ_LIVE_IN;
    else if( m1 != code8 && m1 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m2 = (meta >> 16) & 0xff;
    if( m2 == LIVE_IN )
      bs[2] = READ_LIVE_IN;
    else if( m2 != code8 && m2 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m3 = (meta >> 24) & 0xff;
    if( m3 == LIVE_IN )
      bs[3] = READ_LIVE_IN;
    else if( m3 != code8 && m3 != READ_LIVE_IN )
      __specpriv_misspec(name);
  }

  TADD(worker_time_in_priv_read,start);
}

// For 8-byte loads/stores

// Update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
void __specpriv_private_write_8b(void *ptr)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    uint64_t *shadow = (uint64_t*) ( SHADOW_ADDR | (uint64_t)ptr );
    // TODO: optimize
    if( memchr(shadow, READ_LIVE_IN, sizeof(uint64_t) ) )
      __specpriv_misspec("misspec in private write 8b");
    *shadow = code64;

    update_shadow_range( (uint8_t*)shadow, 8);

    DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );

    TOUT(worker_private_bytes_written += 8);
  }

  TADD(worker_time_in_priv_write,start);
}

// Update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
void __specpriv_private_read_8b(void *ptr, const char *name)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    uint64_t *shadow = (uint64_t*) ( SHADOW_ADDR | (uint64_t)ptr );
    const uint64_t meta = *shadow;
    TOUT(worker_private_bytes_read += 8);

    update_shadow_range( (uint8_t*)shadow, 8 );

    DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );

    if( meta == V64( LIVE_IN ) )
    {
      *shadow = V64( READ_LIVE_IN );
      TADD(worker_time_in_priv_read,start);
      return;
    }
    else if( meta == code64 || meta == V64( READ_LIVE_IN ) )
    {
      TADD(worker_time_in_priv_read,start);
      return;
    }

    // At this point, it's very likely that we
    // will misspeculate...  unless we read
    // 8 bytes and some were defined in this iteration
    // and the rest were live-in.
    uint8_t *bs = (uint8_t*)shadow;

    const uint8_t m0 = (meta >> 0) & 0xff;
    if( m0 == LIVE_IN )
      bs[0] = READ_LIVE_IN;
    else if( m0 != code8 && m0 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m1 = (meta >> 8) & 0xff;
    if( m1 == LIVE_IN )
      bs[1] = READ_LIVE_IN;
    else if( m1 != code8 && m1 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m2 = (meta >> 16) & 0xff;
    if( m2 == LIVE_IN )
      bs[2] = READ_LIVE_IN;
    else if( m2 != code8 && m2 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m3 = (meta >> 24) & 0xff;
    if( m3 == LIVE_IN )
      bs[3] = READ_LIVE_IN;
    else if( m3 != code8 && m3 != READ_LIVE_IN )
      __specpriv_misspec(name);


    const uint8_t m4 = (meta >> 32) & 0xff;
    if( m4 == LIVE_IN )
      bs[4] = READ_LIVE_IN;
    else if( m4 != code8 && m4 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m5 = (meta >> 40) & 0xff;
    if( m5 == LIVE_IN )
      bs[5] = READ_LIVE_IN;
    else if( m5 != code8 && m5 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m6 = (meta >> 48) & 0xff;
    if( m6 == LIVE_IN )
      bs[6] = READ_LIVE_IN;
    else if( m6 != code8 && m6 != READ_LIVE_IN )
      __specpriv_misspec(name);

    const uint8_t m7 = (meta >> 56) & 0xff;
    if( m7 == LIVE_IN )
      bs[7] = READ_LIVE_IN;
    else if( m7 != code8 && m7 != READ_LIVE_IN )
      __specpriv_misspec(name);
  }

  TADD(worker_time_in_priv_read,start);
}

// Update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
void __specpriv_private_write_range_stride(void *base, uint64_t nStrides, uint64_t strideWidth, uint64_t lenPerStride)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    if( nStrides > 0 )
    {
      uint8_t *cbase = (uint8_t*) (SHADOW_ADDR | (uint64_t) base );
      uint8_t *high = cbase + (nStrides-1)*strideWidth + lenPerStride;

      if( cbase < shadow_lowest_inclusive )
        shadow_lowest_inclusive = cbase;
      if( shadow_highest_exclusive < high )
        shadow_highest_exclusive = high;

      DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );

      switch( lenPerStride )
      {
      case 4:
        {
          for(uint64_t i=0; i<nStrides; ++i, cbase += strideWidth)
          {
            uint32_t *ibase = (uint32_t*)cbase;
            const uint32_t meta = *ibase;
            *ibase = code32;

            if( meta  != V32(LIVE_IN) )
            {
              // TODO: optimize this ugly crap.

              const uint8_t m0 = (meta >> 0) & 0xff;
              if( m0 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=4");
              const uint8_t m1 = (meta >> 8) & 0xff;
              if( m1 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=4");
              const uint8_t m2 = (meta >> 16) & 0xff;
              if( m2 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=4");
              const uint8_t m3 = (meta >> 24) & 0xff;
              if( m3 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=4");
            }
          }
        }
        break;

      case 8:
        {
          for(uint64_t i=0; i<nStrides; ++i, cbase += strideWidth)
          {
            uint64_t *ibase = (uint64_t*)cbase;
            const uint64_t meta = *ibase;
            *ibase = code64;

            if( meta != V64(LIVE_IN) )
            {
              // TODO: optimize this ugly crap.

              const uint8_t m0 = (meta >> 0) & 0xff;
              if( m0 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=8");
              const uint8_t m1 = (meta >> 8) & 0xff;
              if( m1 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=8");
              const uint8_t m2 = (meta >> 16) & 0xff;
              if( m2 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=8");
              const uint8_t m3 = (meta >> 24) & 0xff;
              if( m3 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=8");

              const uint8_t m4 = (meta >> 32) & 0xff;
              if( m4 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=8");
              const uint8_t m5 = (meta >> 40) & 0xff;
              if( m5 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=8");
              const uint8_t m6 = (meta >> 48) & 0xff;
              if( m6 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=8");
              const uint8_t m7 = (meta >> 56) & 0xff;
              if( m7 == READ_LIVE_IN )
                __specpriv_misspec("misspec during private write range string, lenPerStride=8");
            }
          }
        }
        break;

      default:
        {
          for(uint64_t i=0; i<nStrides; ++i, cbase += strideWidth)
          {
            // TODO: optimize this.
            if( memchr(cbase, READ_LIVE_IN, lenPerStride) )
              __specpriv_misspec("misspec during private_write_range_stride, lenPerStride=x");
            memset(cbase, code8, lenPerStride);
          }
        }
        break;
      }

      TOUT(worker_private_bytes_written += nStrides * lenPerStride);
    }
  }
  TADD(worker_time_in_priv_write,start);
}

// Update the range [shadow_lowest_inclusive, shadow_highest_exclusive)
void __specpriv_private_read_range_stride(void *base, uint64_t nStrides, uint64_t strideWidth, uint64_t lenPerStride, const char *message)
{
  uint64_t start;
  TIME(start);

  if( ! __specpriv_i_am_main_process() )
  {
    uint8_t *cbase = (uint8_t*) ( SHADOW_ADDR | (uint64_t) base );

    if( nStrides > 0 )
    {
      uint8_t *high = cbase + (nStrides-1)*strideWidth + lenPerStride;

      if( cbase < shadow_lowest_inclusive )
        shadow_lowest_inclusive = cbase;
      if( shadow_highest_exclusive < high )
        shadow_highest_exclusive = high;

      DEBUG( assert( ((uint64_t)shadow_highest_exclusive) <= SHADOW_ADDR + __specpriv_sizeof_private() ) );
    }

    switch( lenPerStride )
    {
      case 4:
        for(uint64_t i=0; i<nStrides; ++i, cbase += strideWidth)
        {
          uint32_t *ibase = (uint32_t*)cbase;
          const uint32_t meta = *ibase;
          if( meta == V32(LIVE_IN) )
            *ibase = V32(READ_LIVE_IN);
          else if( meta != code32 && meta != V32(READ_LIVE_IN) )
            __specpriv_misspec(message);
        }
        break;
      case 8:
        for(uint64_t i=0; i<nStrides; ++i, cbase += strideWidth)
        {
          uint64_t *ibase = (uint64_t*)cbase;
          const uint64_t meta = *ibase;
          if( meta == V64(LIVE_IN) )
            *ibase = V64(READ_LIVE_IN);
          else if( meta != code64 && meta != V64(READ_LIVE_IN) )
            __specpriv_misspec(message);
        }
        break;
      case 16:
        for(uint64_t i=0; i<nStrides; ++i, cbase += strideWidth)
        {
          uint64_t *ibase = (uint64_t*)cbase;
          const uint64_t meta0 = ibase[0];
          if( meta0 == V64(LIVE_IN) )
            ibase[0] = V64(READ_LIVE_IN);
          else if( meta0 != code64 && meta0 != V64(READ_LIVE_IN) )
            __specpriv_misspec(message);

          const uint64_t meta1 = ibase[1];
          if( meta1 == V64(LIVE_IN) )
            ibase[2] = V64(READ_LIVE_IN);
          else if( meta1 != code64 && meta1 != V64(READ_LIVE_IN) )
            __specpriv_misspec(message);
        }
        break;
      default:
        for(uint64_t i=0; i<nStrides; ++i, cbase += strideWidth)
          __specpriv_private_read_range_internal(cbase, &cbase[lenPerStride], message);
        break;
    }

    TOUT(worker_private_bytes_read += nStrides * lenPerStride);
  }

  TADD(worker_time_in_priv_read,start);
}

// partial <-- later(worker,partial)
// where worker, partial are from the same checkpoint-group of iterations.
Bool __specpriv_distill_worker_private_into_partial(
  Checkpoint *partial,
  MappedHeap *partial_priv, MappedHeap *partial_shadow)
{
  const unsigned len = __specpriv_sizeof_private();

  if( len > 0 )
  {
    uint8_t *src_p = (uint8_t*)PRIV_ADDR,           // pointer to worker's private value
            *src_s = (uint8_t*)SHADOW_ADDR,         // pointer to worker's shadow
            *dst_p = (uint8_t*)partial_priv->base,  // pointer to main's private value
            *dst_s = (uint8_t*)partial_shadow->base;// pointer to main's shadow

    DEBUG(
      printf("Distilling %u private bytes from worker 0x%lx/0x%lx into partial 0x%lx/0x%lx\n",
        len, (uint64_t)src_p, (uint64_t)src_s, (uint64_t)dst_p, (uint64_t)dst_s);
      printf("-> fine range is [0x%lx, 0x%lx)\n",
        (uint64_t)shadow_lowest_inclusive, (uint64_t)shadow_highest_exclusive);
    );

    const unsigned bytesPerWord = sizeof(uint64_t) / sizeof(uint8_t);

    const unsigned low  = ROUND_DOWN( shadow_lowest_inclusive - src_s, bytesPerWord ),
                   high = ROUND_UP( shadow_highest_exclusive - src_s, bytesPerWord );

    // TODO: vectorize this.
    for(unsigned i=low; i<high; i += bytesPerWord )
    {
      uint64_t *many = (uint64_t*) &src_s[i];
      if( V64(LIVE_IN) == *many )
        continue;

      // ss != LIVE_IN

      for(unsigned j=0; j<bytesPerWord; ++j)
      {
        const unsigned k = i+j;
        const uint8_t ss = src_s[k], ds = dst_s[k];

        if( ss == READ_LIVE_IN )
        {
          if( ds == LIVE_IN )
          {
            dst_s[k] = READ_LIVE_IN;
            continue;
          }
          else if( ds == READ_LIVE_IN )
          {
            continue;
          }
          else
          {
            // Misspeculate!
            return 1;
          }
        }
        else if( /* ss != LIVE_IN, READ_LIVE_IN and */ ds == READ_LIVE_IN )
        {
          // Misspeculate!
          return 1;
        }

        // My copy is newer than the partial.
        if( ss > ds )
        {
          dst_p[k] = src_p[k];
          dst_s[k] = ss;
        }
      }
    }
  }

  // Update [low,high) ranges.
  if( shadow_lowest_inclusive < partial->shadow_lowest_inclusive )
    partial->shadow_lowest_inclusive = shadow_lowest_inclusive;
  if( partial->shadow_highest_exclusive < shadow_highest_exclusive )
    partial->shadow_highest_exclusive = shadow_highest_exclusive;

  __specpriv_reset_shadow_range();
  return 0;
}

// partial <-- later(committed,partial)
// where committed comes from an EARLIER checkpoint-group of iterations.
Bool __specpriv_distill_committed_private_into_partial(
  Checkpoint *commit, MappedHeap *commit_priv, MappedHeap *commit_shadow,
  Checkpoint *partial, MappedHeap *partial_priv, MappedHeap *partial_shadow)
{
  const unsigned len = __specpriv_sizeof_private();

  if( len > 0 )
  {
    uint8_t *src_p = (uint8_t*)commit_priv->base,   // ptr to committed private value
            *src_s = (uint8_t*)commit_shadow->base, // ptr to committed shadow
            *dst_p = (uint8_t*)partial_priv->base,  // ptr to partial private value
            *dst_s = (uint8_t*)partial_shadow->base;// ptr to partial shadow

    DEBUG(
      printf("Distilling %u private bytes from committed 0x%lx/0x%lx into partial 0x%lx/0x%lx\n",
        len, (uint64_t)src_p, (uint64_t)src_s, (uint64_t)dst_p, (uint64_t)dst_s);
      printf("-> fine range is [0x%lx, 0x%lx)\n",
        (uint64_t)commit->shadow_lowest_inclusive, (uint64_t)commit->shadow_highest_exclusive);
    );

    const unsigned low  = commit->shadow_lowest_inclusive - (uint8_t*)SHADOW_ADDR,
                   high = commit->shadow_highest_exclusive - (uint8_t*)SHADOW_ADDR;

    // TODO make this faster; vectorize?
    for(unsigned i=low; i<high; ++i)
    {
      const uint8_t ds = dst_s[i];

      if( ds == LIVE_IN )
      {
        if( WAS_WRITTEN_RECENTLY( src_s[i] ) )
        {
          dst_p[i] = src_p[i];
          dst_s[i] = OLD_ITERATION;
        }
      }

      else if( ds == READ_LIVE_IN )
      {
        if( WAS_WRITTEN_RECENTLY( src_s[i] ) )
        {
          // Misspeculate!
          return 1;
        }
      }
    }
  }

  // Update [low,high) ranges.
  if( commit->shadow_lowest_inclusive < partial->shadow_lowest_inclusive )
    partial->shadow_lowest_inclusive = commit->shadow_lowest_inclusive;
  if( partial->shadow_highest_exclusive < commit->shadow_highest_exclusive )
    partial->shadow_highest_exclusive = commit->shadow_highest_exclusive;

  return 0;
}


Bool __specpriv_distill_committed_private_into_main(Checkpoint *commit, MappedHeap *commit_priv, MappedHeap *commit_shadow)
{
  const unsigned len = __specpriv_sizeof_private();

  if( len > 0 )
  {
    uint8_t *src_p = (uint8_t*)commit_priv->base,   // ptr to committed private value
            *src_s = (uint8_t*)commit_shadow->base, // ptr to committed shadow
            *dst_p = (uint8_t*)PRIV_ADDR;           // ptr to partial private value

    DEBUG(
      printf("Distilling %u private bytes from committed 0x%lx/0x%lx into main 0x%lx/-\n",
        len, (uint64_t)src_p, (uint64_t)src_s, (uint64_t)dst_p);
      printf("-> fine range is [0x%lx, 0x%lx)\n",
        (uint64_t)commit->shadow_lowest_inclusive, (uint64_t)commit->shadow_highest_exclusive);
    );

    const unsigned low = commit->shadow_lowest_inclusive - (uint8_t*)SHADOW_ADDR,
                   high = commit->shadow_highest_exclusive - (uint8_t*)SHADOW_ADDR;

    // TODO: vectorize this.
    for(unsigned i=low; i<high; ++i)
      if( WAS_WRITTEN_EVER( src_s[i] ) )
        dst_p[i] = src_p[i];
  }

  return 0;
}




