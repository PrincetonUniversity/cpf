#include "internals/utils.h"
#include "internals/debug.h"

#include <stdio.h>

#include <emmintrin.h>
#include <smmintrin.h>
#include <xmmintrin.h>

bool is_zero_page(uint8_t* ptr)
{
  for (unsigned i = 0 ; i < 4096 ; i += 128)
  {
    __m128i buf;
    buf = _mm_load_si128( (__m128i*)(ptr+i) );
    if (!_mm_testz_si128(buf, buf))
      return false;
    buf = _mm_load_si128( (__m128i*)(ptr+i+16) );
    if (!_mm_testz_si128(buf, buf))
      return false;
    buf = _mm_load_si128( (__m128i*)(ptr+i+32) );
    if (!_mm_testz_si128(buf, buf))
      return false;
    buf = _mm_load_si128( (__m128i*)(ptr+i+48) );
    if (!_mm_testz_si128(buf, buf))
      return false;
    buf = _mm_load_si128( (__m128i*)(ptr+i+64) );
    if (!_mm_testz_si128(buf, buf))
      return false;
    buf = _mm_load_si128( (__m128i*)(ptr+i+80) );
    if (!_mm_testz_si128(buf, buf))
      return false;
    buf = _mm_load_si128( (__m128i*)(ptr+i+96) );
    if (!_mm_testz_si128(buf, buf))
      return false;
    buf = _mm_load_si128( (__m128i*)(ptr+i+112) );
    if (!_mm_testz_si128(buf, buf))
      return false;
  }

  return true;
}

void set_zero_page(uint8_t* ptr)
{
  __m128i zero = _mm_set1_epi32(0);
  for (unsigned i = 0 ; i < 4096 ; i += 128)
  {
    _mm_store_si128( (__m128i*)(ptr+i), zero );
    _mm_store_si128( (__m128i*)(ptr+i+16), zero );
    _mm_store_si128( (__m128i*)(ptr+i+32), zero );
    _mm_store_si128( (__m128i*)(ptr+i+48), zero );
    _mm_store_si128( (__m128i*)(ptr+i+64), zero );
    _mm_store_si128( (__m128i*)(ptr+i+80), zero );
    _mm_store_si128( (__m128i*)(ptr+i+96), zero );
    _mm_store_si128( (__m128i*)(ptr+i+112), zero );
  }
}

void dump_page(uint8_t* page)
{
  for (unsigned i = 0 ; i < 4096 ; i += 128)
  {
    for (unsigned j = 0 ; j < 128 ; j++)
      specpriv_smtx::DBG("%x ", page[i+j]);
    specpriv_smtx::DBG("\n");
  }
}
