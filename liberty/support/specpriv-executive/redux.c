#include <stdint.h>
#include <xmmintrin.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <float.h>

#include "config.h"
#include "api.h"
#include "heap.h"
#include "fiveheaps.h"
#include "timer.h"

#include "redux.h"

void __specpriv_redux_write_range(uint8_t *ptr, unsigned size) {}

// Perform reduction on an array of signed 32-bit ints
static void __specpriv_reduce_i32_add(int32_t *src_au, int32_t *dst_au, uint32_t size_bytes)
{
  // Simple case: a single int32_t.
  if( size_bytes == sizeof(int32_t) )
  {
    DEBUG(printf("%d + %d ", *dst_au, *src_au));
    *dst_au += *src_au;
    *src_au = 0;
    DEBUG(printf("-> %d\n", *dst_au));
    return;
  }

  // General case: many int32_t.
  __m128i zero = _mm_setzero_si128();
  // Manually vectorized

  // Number of int adds (rounded up to an even int)
  const int Nint = (size_bytes + sizeof(int32_t) - 1) / sizeof(int32_t);
  // Number of int adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int intsPerVec = sizeof(__m128) / sizeof(int);
  const int N = (Nint + intsPerVec - 1) & ~( intsPerVec - 1 );

#if REDUCTION == VECTOR
  // Find the last index at which the worker has a non-zero value
  int hi;
  for(hi = N-4; hi>=0; hi -= intsPerVec)
  {
    __m128i sv = _mm_load_si128( (__m128i*) &src_au[hi] );
    const __m128i vcmp = (__m128i) _mm_cmpeq_epi32(sv, zero);
    const uint16_t test = _mm_movemask_epi8(vcmp);
    if( test != 0x0ffff )
      break;
  }

  // Find the first index at which the worker has a non-zero value.
  int lo;
  for(lo=0; lo<hi; lo += intsPerVec)
  {
    __m128i sv = _mm_load_si128( (__m128i*)&src_au[lo] );
    const __m128i vcmp = (__m128i) _mm_cmpeq_epi32(sv, zero);
    const uint16_t test = _mm_movemask_epi8(vcmp);
    if( test != 0x0ffff )
      break;
  }

  // Perform the reduction only within that range.
  int i;
  for(i=lo; i<=hi; i += intsPerVec)
  {
    __m128i sv = _mm_load_si128( (__m128i*) &src_au[i] );
    __m128i sum = _mm_add_epi32( _mm_load_si128( (__m128i*) &dst_au[i] ), sv);
    _mm_store_si128( (__m128i*)&dst_au[i], sum );
  }

  // Reset the reduction for next invocation.
  uint8_t *first = (uint8_t*) & src_au[lo];
  uint8_t *last = (uint8_t*) & src_au[ hi + 1 ];
  if( first < last )
  {
    unsigned len = last - first;
    memset( (void*)first, 0, len );
  }
#else
# error todo
  /*
  int i;
  for(i=0; i<N; ++i)
  {
    dst_au[i] += src_au[i];
    src_au[i] = 0.0f;
  }
  */
#endif
}


/*
// Perform reduction on an array of 32-bit floats
static void __specpriv_reduce_f32_add(float *src_au, float *dst_au, uint32_t size_bytes)
{
  // Simple case: a single float.
  if( size_bytes == sizeof(float) )
  {
    DEBUG(printf("%f + %f ", *dst_au, *src_au));
    *dst_au += *src_au;
    *src_au = 0.0f;
    DEBUG(printf("-> %f\n", *dst_au));
    return;
  }

  // General case: many floats.
  __m128 zero = _mm_setzero_ps();
  // Manually vectorized

  // Number of float adds (rounded up to an even int)
  const int Nfloat = (size_bytes + sizeof(float) - 1) / sizeof(float);
  // Number of float adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int floatsPerVec = sizeof(__m128) / sizeof(float);
  const int N = (Nfloat + floatsPerVec - 1) & ~( floatsPerVec - 1 );

#if REDUCTION == VECTOR
  // Find the last index at which the worker has a non-zero value
  int hi;
  for(hi = N-4; hi>=0; hi -= floatsPerVec)
  {
    __m128 sv = _mm_load_ps( &src_au[hi] );
    const __m128i vcmp = (__m128i) _mm_cmpeq_ps(sv, zero);
    const uint16_t test = _mm_movemask_epi8(vcmp);
    if( test == 0 )
      break;
  }

  // Find the first index at which the worker has a non-zero value.
  int lo;
  for(lo=0; lo<hi; lo += floatsPerVec)
  {
    __m128 sv = _mm_load_ps( &src_au[lo] );
    const __m128i vcmp = (__m128i) _mm_cmpeq_ps(sv, zero);
    const uint16_t test = _mm_movemask_epi8(vcmp);
    if( test == 0 )
      break;
  }

  // Perform the reduction only within that range.
  int i;
  for(i=lo; i<=hi; i += floatsPerVec)
  {
    __m128 sv = _mm_load_ps( &src_au[i] );
    __m128 sum = _mm_add_ps( _mm_load_ps( &dst_au[i] ), sv);
    _mm_store_ps( &dst_au[i], sum );
  }

  // Reset the reduction for next invocation.
  uint8_t *first = (uint8_t*) & src_au[lo];
  uint8_t *last = (uint8_t*) & src_au[ hi + 1 ];
  if( first < last )
  {
    unsigned len = last - first;
    memset( (void*)first, 0, len );
  }
#else
  int i;
  for(i=0; i<N; ++i)
  {
    dst_au[i] += src_au[i];
    src_au[i] = 0.0f;
  }
#endif
}
*/

// Perform reduction on an array of 32-bit floats
static void __specpriv_reduce_f32_add(float *src_au, float *dst_au, uint32_t size_bytes)
{
  // Simple case: a single float.
  if( size_bytes == sizeof(float) )
  {
    DEBUG(printf("%f + %f ", *dst_au, *src_au));
    *dst_au += *src_au;
    *src_au = 0.0f;
    DEBUG(printf("-> %f\n", *dst_au));
    return;
  }

  // General case: many floats.
//  const __m128 zero = _mm_setzero_ps();
  // Manually vectorized

  // Number of float adds (rounded up to an even int)
  const int Nfloat = (size_bytes + sizeof(float) - 1) / sizeof(float);
  // Number of float adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int floatsPerVec = sizeof(__m128) / sizeof(float);
  const int N = (Nfloat + floatsPerVec - 1) & ~( floatsPerVec - 1 );

#if REDUCTION == VECTOR

  int hi = N-floatsPerVec;
  int lo=0;

  // Find the last index at which the worker has a non-zero value
  for(; hi>=0; hi -= floatsPerVec)
  {
    const __m128 sv = _mm_load_ps( &src_au[hi] );
    const __m128i vcmp = (__m128i) _mm_cmpeq_ps(sv, _mm_setzero_ps());
    const uint16_t test = _mm_movemask_epi8(vcmp);
    //DEBUG(printf(" test %d\n", test));
    if( test != 0x0ffff ) {
      DEBUG(printf(" test %d\n", test));
      break;
    }
  }

  // Find the first index at which the worker has a non-zero value.
  for(; lo<hi; lo += floatsPerVec)
  {
    const __m128 sv = _mm_load_ps( &src_au[lo] );
    const __m128i vcmp = (__m128i) _mm_cmpeq_ps(sv, _mm_setzero_ps());
    const uint16_t test = _mm_movemask_epi8(vcmp);
    if( test != 0x0ffff ) {
      DEBUG(printf(" test %d\n", test));
      break;
    }
  }


  // Perform the reduction only within that range.
  int i;
  for(i=lo; i<=hi; i += floatsPerVec)
  {
    const __m128 sv = _mm_load_ps( &src_au[i] );
    const __m128 sum = _mm_add_ps( _mm_load_ps( &dst_au[i] ), sv);
    _mm_store_ps( &dst_au[i], sum );
//    _mm_store_ps( &dst_au[i], _mm_add_ps( _mm_load_ps( &dst_au[i] ), _mm_load_ps( &src_au[i] )/*sv*/) );
  }

  DEBUG(printf("Done with performing reduction\n"));

  // Reset the reduction for next invocation.
  uint8_t *first = (uint8_t*) & src_au[lo];
  uint8_t *last = (uint8_t*) & src_au[ hi + 1 ];
  if( first < last )
  {
    unsigned len = last - first;
    memset( (void*)first, 0, len );
  }
#else
  int i;
  for(i=0; i<N; ++i)
  {
    dst_au[i] += src_au[i];
    src_au[i] = 0.0f;
  }
#endif
  DEBUG(printf("__specpriv_reduce_f32_add\n"));
}



// Perform reduction on an array of 64-bit doubles
static void __specpriv_reduce_f64_add(double *src_au, double *dst_au, uint32_t size_bytes)
{
  // Simple case: a single double.
  if( size_bytes == sizeof(double) )
  {
    DEBUG(printf("%lf + %lf ", *dst_au, *src_au));
    *dst_au += *src_au;
    *src_au = 0.0;
    DEBUG(printf("-> %lf\n", *dst_au));
    return;
  }

  // General case: many doubles.
  __m128d zero = _mm_setzero_pd();
  // Manually vectorized

  // Number of double adds (rounded up to an even int)
  const int Ndoubles = (size_bytes + sizeof(double) - 1) / sizeof(double);
  // Number of double adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int doublesPerVec = sizeof(__m128) / sizeof(double);
  const int N = (Ndoubles + doublesPerVec - 1) & ~( doublesPerVec - 1 );

#if REDUCTION == VECTOR
  // Find the last index at which the worker has a non-zero value
  int hi;
  for(hi = N-doublesPerVec; hi>=0; hi -= doublesPerVec)
  {
    __m128d sv = _mm_load_pd( &src_au[hi] );
    const __m128i vcmp = (__m128i) _mm_cmpeq_pd(sv, zero);
    const uint16_t test = _mm_movemask_epi8(vcmp);
    if( test != 0x0ffff )
      break;
  }

  // Find the first index at which the worker has a non-zero value.
  int lo;
  for(lo=0; lo<hi; lo += doublesPerVec)
  {
    __m128d sv = _mm_load_pd( &src_au[lo] );
    const __m128i vcmp = (__m128i) _mm_cmpeq_pd(sv, zero);
    const uint16_t test = _mm_movemask_epi8(vcmp);
    if( test != 0x0ffff )
      break;
  }

  // Perform the reduction only within that range.
  int i;
  for(i=lo; i<=hi; i += doublesPerVec)
  {
    __m128d sv = _mm_load_pd( &src_au[i] );
    __m128d sum = _mm_add_pd( _mm_load_pd( &dst_au[i] ), sv);
    _mm_store_pd( &dst_au[i], sum );
  }

  // Reset the reduction for next invocation.
  uint8_t *first = (uint8_t*) & src_au[lo];
  uint8_t *last = (uint8_t*) & src_au[ hi + 1 ];
  if( first < last )
  {
    unsigned len = last - first;
    memset( (void*)first, 0, len );
  }
#else
  int i;
  for(i=0; i<N; ++i)
  {
    dst_au[i] += src_au[i];
    src_au[i] = 0.0;
  }
#endif
}

static void __specpriv_reduce_u64_max(uint64_t *src_au, uint64_t *dst_au,
                                      uint32_t size_bytes, void *src_dep_au,
                                      void *dst_dep_au, uint32_t dep_size_bytes,
                                      uint8_t depType, Iteration srcLastUpIter,
                                      Iteration *dstLastUpIter) {
  // Simple case: a single int64_t.
  if( size_bytes == sizeof(uint64_t) )
  {
    if (dep_size_bytes == 0) {
      if ( *src_au > *dst_au ) {
        *dst_au = *src_au;
        *dstLastUpIter = srcLastUpIter;
      }
      *src_au = 0;
      return;
    }
    else if (dep_size_bytes == 4) {
      if (depType == 2) {
        float *dstDep = (float *)dst_dep_au;
        float *srcDep = (float *)src_dep_au;
        DEBUG(printf("Performing a u64-max reduction on address 0x%lx "
                     "depending on address 0x%lx\n",
                     (uint64_t)src_au, (uint64_t)src_dep_au));
        DEBUG(printf("dst_dep_au: %f, src_dep_au:%f\n", *dstDep, *srcDep));
        DEBUG(printf("dstLastUpIter: %u, srcLastUpIter:%u\n", *dstLastUpIter,
                     srcLastUpIter));

        if (*srcDep > *dstDep ||
            (*srcDep == *dstDep && srcLastUpIter < *dstLastUpIter)) {
          *dst_au = *src_au;
        }
      }
      else if (depType == 1) {
        int32_t *dstDep = (int32_t*) dst_dep_au;
        int32_t *srcDep = (int32_t*) src_dep_au;
        if (*srcDep > *dstDep ||
            (*srcDep == *dstDep && srcLastUpIter < *dstLastUpIter)) {
          *dst_au = *src_au;
        }
      }
      else if (depType == 0) {
        uint32_t *dstDep = (uint32_t*) dst_dep_au;
        uint32_t *srcDep = (uint32_t*) src_dep_au;
        if (*srcDep > *dstDep ||
            (*srcDep == *dstDep && srcLastUpIter < *dstLastUpIter)) {
          *dst_au = *src_au;
        }
      }
      else {
        assert(0 && "depType is not in [0,2] ?!");
      }
      *src_au = 0;
      return;
    }
    else
      assert(0 && "Not yet implemented");
  }

  assert(dep_size_bytes == 0 && "Not yet implemented");

  // General case: many int64_ts.
  // Manually vectorized

  // Number of int64_ts (rounded up to an even int)
  const int Nuint64_t = (size_bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t);
  // Number of int64_t adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int uint64_tPerVec = sizeof(__m128d) / sizeof(uint64_t);
  const int N = (Nuint64_t + uint64_tPerVec - 1) & ~( uint64_tPerVec - 1 );

  //TODO: fix vector operations. following are meant for double
//#if REDUCTION == VECTOR
  // Perform the reduction
  /*
  int i;
  for(i=0; i<N; i+= uint64_tPerVec)
  {
    const __m128d src = _mm_load_pd( &src_au[i] );
    const __m128d d0 = _mm_load_pd( &dst_au[i] );
    const __m128d max = _mm_max_pd( d0, src);
    _mm_store_pd( &dst_au[i], max );
  }

  // Reset the reduction for next invocation.
  uint8_t *first = (uint8_t*) & src_au[0];
  uint8_t *last = (uint8_t*) & src_au[ N ];

  if( first < last )
  {
    unsigned len = last - first;

    memset( (void*)first, 0, len );
  }
  */
//#else
  int i;
  for(i=0; i<N; ++i)
  {
    const uint64_t src = src_au[i];
    const uint64_t d0 = dst_au[i];
    if( src > d0 )
      dst_au[i] = src;

    src_au[i] = 0;
  }
//#endif
}

static void __specpriv_reduce_f32_max(float *src_au, float *dst_au,
                                      uint32_t size_bytes,
                                      Iteration srcLastUpIter,
                                      Iteration *dstLastUpIter) {
  // Simple case: a single float.
  if( size_bytes == sizeof(float) )
  {
    if (*src_au > *dst_au) {
      *dst_au = *src_au;
      *dstLastUpIter = srcLastUpIter;
    } else if (*src_au == *dst_au && srcLastUpIter < *dstLastUpIter) {
      *dstLastUpIter = srcLastUpIter;
    }

    *src_au = -FLT_MAX;
    return;
  }

  // General case: many floats.
  // Manually vectorized

  // Number of floats (rounded up to an even int)
  const int Nfloat = (size_bytes + sizeof(float) - 1) / sizeof(float);
  // Number of float adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int floatPerVec = sizeof(__m128d) / sizeof(float);
  const int N = (Nfloat + floatPerVec - 1) & ~( floatPerVec - 1 );

#if REDUCTION == VECTOR
  // Perform the reduction
  int i;
  for(i=0; i<N; i+= floatPerVec)
  {
    const __m128 src = _mm_load_ps( &src_au[i] );
    const __m128 d0 = _mm_load_ps( &dst_au[i] );
    const __m128 max = _mm_max_ps( d0, src);
    _mm_store_ps( &dst_au[i], max );
  }

  //TODO: properly do identity for floats
  // maybe it is not necessary to set back to identity
  // TODO: examine whether it needs to be implemented
  // the reductions are initialized before the loop invocation by the program
  /*
  // Reset the reduction for next invocation.
  uint8_t *first = (uint8_t*) & src_au[0];
  uint8_t *last = (uint8_t*) & src_au[ N ];

  if( first < last )
  {
    unsigned len = last - first;

    //how is a signed float represented in bits in memory??? maybe write 10000000 for each byte
    // TODO: should be identity -INF
    memset( (void*)first, 0, len );
  }
  */
#else
  int i;
  for(i=0; i<N; ++i)
  {
    const float src = src_au[i];
    const float d0 = dst_au[i];
    if( src > d0 )
      dst_au[i] = src;

    src_au[i] = -FLT_MAX;
  }
#endif
}


static void __specpriv_reduce_f64_max(double *src_au, double *dst_au, uint32_t size_bytes)
{
  // Simple case: a single double.
  if( size_bytes == sizeof(double) )
  {
    if ( *src_au > *dst_au )
      *dst_au = *src_au;
    *src_au = -DBL_MAX;
    return;
  }

  // General case: many doubles.
  // Manually vectorized

  // Number of doubles (rounded up to an even int)
  const int Ndouble = (size_bytes + sizeof(double) - 1) / sizeof(double);
  // Number of double adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int doublePerVec = sizeof(__m128d) / sizeof(double);
  const int N = (Ndouble + doublePerVec - 1) & ~( doublePerVec - 1 );

#if REDUCTION == VECTOR
  // Perform the reduction
  int i;
  for(i=0; i<N; i+= doublePerVec)
  {
    const __m128d src = _mm_load_pd( &src_au[i] );
    const __m128d d0 = _mm_load_pd( &dst_au[i] );
    const __m128d max = _mm_max_pd( d0, src);
    _mm_store_pd( &dst_au[i], max );
  }

  //TODO: properly do identity for doubles
  // maybe it is not necessary to set back to identity
  // TODO: examine whether it needs to be implemented
  // the reductions are initialized before the loop invocation by the program
  /*
  // Reset the reduction for next invocation.
  uint8_t *first = (uint8_t*) & src_au[0];
  uint8_t *last = (uint8_t*) & src_au[ N ];

  if( first < last )
  {
    unsigned len = last - first;

    //how is a signed double represented in bits in memory??? maybe write 10000000 for each byte
    // TODO: should be identity -INF
    memset( (void*)first, 0, len );
  }
  */
#else
  int i;
  for(i=0; i<N; ++i)
  {
    const double src = src_au[i];
    const double d0 = dst_au[i];
    if( src > d0 )
      dst_au[i] = src;

    src_au[i] = -DBL_MAX;
  }
#endif
}

static void __specpriv_initialize_f32_max(float *au, uint32_t size_bytes) {
  // Simple case: a single float.
  if( size_bytes == sizeof(float) )
  {
    *au = -FLT_MAX;
    return;
  }

  // General case: many floats.
  // Manually vectorized

  // Number of floats (rounded up to an even int)
  const int Nfloat = (size_bytes + sizeof(float) - 1) / sizeof(float);
  // Number of float adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int floatPerVec = sizeof(__m128d) / sizeof(float);
  const int N = (Nfloat + floatPerVec - 1) & ~( floatPerVec - 1 );

//#if REDUCTION == VECTOR
/*
 // Reset the reduction
  uint8_t *first = (uint8_t*) & au[0];
  uint8_t *last = (uint8_t*) & au[ N ];

  if( first < last )
  {
    unsigned len = last - first;

    //how is a signed float represented in bits in memory??? maybe write 10000000 for each byte
    // TODO: should be identity -INF
    memset( (void*)first, 0, len );
  }
*/
//#else
  int i;
  for(i=0; i<N; ++i)
  {
    au[i] = -FLT_MAX;
  }
//#endif
}

static void __specpriv_initialize_f64_max(double *au, uint32_t size_bytes) {
  // Simple case: a single double.
  if( size_bytes == sizeof(double) )
  {
    *au = -DBL_MAX;
    return;
  }

  // General case: many doubles.
  // Manually vectorized

  // Number of doubles (rounded up to an even int)
  const int Ndouble = (size_bytes + sizeof(double) - 1) / sizeof(double);
  // Number of double adds (rounded up to an even number of vector ops)
  // (this is safe, because all AUs are rounded up to a multiple of
  //  16-bytes in heap_alloc)
  const int doublePerVec = sizeof(__m128d) / sizeof(double);
  const int N = (Ndouble + doublePerVec - 1) & ~( doublePerVec - 1 );

//#if REDUCTION == VECTOR
/*
 // Reset the reduction
  uint8_t *first = (uint8_t*) & au[0];
  uint8_t *last = (uint8_t*) & au[ N ];

  if( first < last )
  {
    unsigned len = last - first;

    //how is a signed double represented in bits in memory??? maybe write 10000000 for each byte
    // TODO: should be identity -INF
    memset( (void*)first, 0, len );
  }
*/
//#else
  int i;
  for(i=0; i<N; ++i)
  {
    au[i] = -DBL_MAX;
  }
//#endif
}

void __specpriv_initialize_reductions(void *au, ReductionInfo *info) {
  switch(info->type)
  {
    // Signed/unsigned integer sum, Floating point sum and nsigned integer max
    // do not need special initialization (zeroing out is fine)
    case Add_i8:
    case Add_i16:
    case Add_i64:
    case Add_i32:
    case Add_f32:
    case Add_f64:
    case Max_u8:
    case Max_u16:
    case Max_u32:
    case Max_u64:
      break;

// Signed integer max
    case Max_i8:
    case Max_i16:
    case Max_i32:
    case Max_i64:
      assert(0 && "Not yet implemented");
      break;

// Floating point max
    case Max_f32:
      __specpriv_initialize_f32_max((float *)au, info->size);
      break;

    case Max_f64:
      __specpriv_initialize_f64_max((double *)au, info->size);
      break;

// Signed integer min
    case Min_i8:
    case Min_i16:
    case Min_i32:
    case Min_i64:
      assert(0 && "Not yet implemented");
      break;

// Unsigned integer min
    case Min_u8:
    case Min_u16:
    case Min_u32:
    case Min_u64:
      assert(0 && "Not yet implemented");
      break;

// Floating point min
    case Min_f32:
    case Min_f64:
      assert(0 && "Not yet implemented");
      break;

    default:
      break;
  }
}

static void __specpriv_do_reduction_pp(void *src_au, void *dst_au,
                                       ReductionInfo *info, void *src_dep_au,
                                       void *dst_dep_au,
                                       Iteration srcLastUpIter,
                                       Iteration *dstLastUpIter) {
  switch(info->type)
  {

// Signed/unsigned integer sum
    case Add_i8:
    case Add_i16:
    case Add_i64:
      assert(0 && "Not yet implemented");
      break;

    case Add_i32:
      __specpriv_reduce_i32_add( (int32_t*)src_au, (int32_t*)dst_au, info->size);
      break;


// Floating point sum.
    case Add_f32:
      DEBUG(printf("Performing a f32-add reduction len %u on address 0x%lx -> 0x%lx\n", info->size, (uint64_t)src_au, (uint64_t)dst_au));
      __specpriv_reduce_f32_add( (float*)src_au, (float*)dst_au, info->size);
      break;

    case Add_f64:
      __specpriv_reduce_f64_add( (double*)src_au, (double*)dst_au, info->size);
      break;

// Signed integer max
    case Max_i8:
    case Max_i16:
    case Max_i32:
    case Max_i64:
      assert(0 && "Not yet implemented");
      break;

// Unsigned integer max
    case Max_u8:
    case Max_u16:
    case Max_u32:
      assert(0 && "Not yet implemented");
      break;
    case Max_u64:
      DEBUG(printf("Performing a u64-max reduction on address 0x%lx\n",
                   (uint64_t)src_au));
      __specpriv_reduce_u64_max((uint64_t *)src_au, (uint64_t *)dst_au,
                                info->size, src_dep_au, dst_dep_au,
                                info->depSize, info->depType,
                                srcLastUpIter, dstLastUpIter);
      break;

// Floating point max
    case Max_f32:
      assert(info->depSize == 0 && "Not yet implemented");
      DEBUG(printf("Performing a f32-max reduction on address 0x%lx\n", (uint64_t)src_au));
      __specpriv_reduce_f32_max((float *)src_au, (float *)dst_au, info->size,
                                srcLastUpIter, dstLastUpIter);
      break;

    case Max_f64:
      assert(info->depSize == 0 && "Not yet implemented");
      DEBUG(printf("Performing a f64-max reduction on address 0x%lx\n", (uint64_t)src_au));
      __specpriv_reduce_f64_max((double*)src_au, (double*)dst_au, info->size);
      break;

// Signed integer min
    case Min_i8:
    case Min_i16:
    case Min_i32:
    case Min_i64:
      assert(0 && "Not yet implemented");
      break;

// Unsigned integer min
    case Min_u8:
    case Min_u16:
    case Min_u32:
    case Min_u64:
      assert(0 && "Not yet implemented");
      break;

// Floating point min
    case Min_f32:
    case Min_f64:
      assert(0 && "Not yet implemented");
      break;

    default:
      break;
  }
}

static void __specpriv_do_reduction_hh(
    MappedHeap * src_redux, MappedHeap * dst_redux, ReductionInfo * info,
    Iteration srcLastUpIter, Iteration *dstLastUpIter) {
  void *native_au = info->au;

  void *src_au = heap_translate( native_au, src_redux );
  void *dst_au = heap_translate( native_au, dst_redux );

  void *src_dep_au = NULL;
  void *dst_dep_au = NULL;
  if (info->depSize) {
    src_dep_au = heap_translate(info->depAU, src_redux);
    dst_dep_au = heap_translate(info->depAU, dst_redux);
  }

  __specpriv_do_reduction_pp(src_au, dst_au, info, src_dep_au, dst_dep_au,
                             srcLastUpIter, dstLastUpIter);
}

Bool __specpriv_distill_worker_redux_into_partial(MappedHeap * partial_redux,
                                                  Iteration *partialLastUpIter) {
  assert( !__specpriv_i_am_main_process() );

  DEBUG(printf("__specpriv_distill_worker_redux_into_partial\n"));

  Iteration srcUpdateIter = __specpriv_last_redux_update_iter();

  ReductionInfo *info = __specpriv_first_reduction_info();
  for(; info; info = info->next)
  {
    void *native_au = info->au;
    void *dst_au = heap_translate(native_au, partial_redux);

    void *dst_dep_au = NULL;
    if (info->depSize) {
      dst_dep_au = heap_translate(info->depAU, partial_redux);
    }

    __specpriv_do_reduction_pp(native_au, dst_au, info, info->depAU, dst_dep_au,
                               srcUpdateIter, partialLastUpIter);
  }

  return 0;
}

Bool __specpriv_distill_committed_redux_into_partial(
    MappedHeap * committed, MappedHeap * partial, Iteration committedLastUpIter,
    Iteration *partialLastUpIter) {
  //  assert( !__specpriv_i_am_main_process() );

  DEBUG(printf("__specpriv_distill_committed_redux_into_partial\n"));

  ReductionInfo *info = __specpriv_first_reduction_info();
  for(; info; info = info->next)
    __specpriv_do_reduction_hh(committed, partial, info, committedLastUpIter, partialLastUpIter);

  return 0;
}

Bool __specpriv_distill_committed_redux_into_main(
    MappedHeap * committed, Iteration committedLastUpIter) {
  assert( __specpriv_i_am_main_process() );

  DEBUG(printf("__specpriv_distill_committed_redux_into_main\n"));

  Iteration dstUpdateIter = __specpriv_last_redux_update_iter();

  ReductionInfo *info = __specpriv_first_reduction_info();
  for(; info; info = info->next)
  {
    void *native_au = info->au;
    void *src_au = heap_translate(native_au, committed);

    void *src_dep_au = NULL;
    if (info->depSize) {
      src_dep_au = heap_translate(info->depAU, committed);
    }

    __specpriv_do_reduction_pp(src_au, native_au, info, src_dep_au, info->depAU,
                               committedLastUpIter, &dstUpdateIter);
  }

  return 0;
}


