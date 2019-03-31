#include <stdint.h>
#include <xmmintrin.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

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

static void __specpriv_reduce_f64_max(double *src_au, double *dst_au, uint32_t size_bytes)
{
  // Simple case: a single double.
  if( size_bytes == sizeof(double) )
  {
    *dst_au += *src_au;
    *src_au = 0.0; // TODO: should be identity
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

  // Reset the reduction for next invocation.
  uint8_t *first = (uint8_t*) & src_au[0];
  uint8_t *last = (uint8_t*) & src_au[ N ];

  if( first < last )
  {
    unsigned len = last - first;

    // TODO: should be identity -INF
    memset( (void*)first, 0, len );
  }
#else
  int i;
  for(i=0; i<N; ++i)
  {
    const double src = src_au[i];
    const double d0 = dst_au[i];
    if( src > d0 )
      dst_au[i] = src;

    // TODO: should be identity -INF
    src_au[i] = 0.0;
  }
#endif
}


static void __specpriv_do_reduction_pp(void *src_au, void *dst_au, ReductionInfo *info)
{
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
    case Max_u64:
      assert(0 && "Not yet implemented");
      break;

// Floating point max
    case Max_f32:
      assert(0 && "Not yet implemented");
      break;

    case Max_f64:
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

static void __specpriv_do_reduction_hh(MappedHeap *src_redux, MappedHeap *dst_redux, ReductionInfo *info)
{
  void *native_au = info->au;

  void *src_au = heap_translate( native_au, src_redux );
  void *dst_au = heap_translate( native_au, dst_redux );

  __specpriv_do_reduction_pp(src_au, dst_au, info);
}

Bool __specpriv_distill_worker_redux_into_partial(MappedHeap *partial_redux)
{
  assert( !__specpriv_i_am_main_process() );

  ReductionInfo *info = __specpriv_first_reduction_info();
  for(; info; info = info->next)
  {
    void *native_au = info->au;
    void *dst_au = heap_translate(native_au, partial_redux);

    __specpriv_do_reduction_pp( native_au, dst_au, info);
  }

  return 0;
}

Bool __specpriv_distill_committed_redux_into_partial(MappedHeap *committed, MappedHeap *partial)
{
//  assert( !__specpriv_i_am_main_process() );

  ReductionInfo *info = __specpriv_first_reduction_info();
  for(; info; info = info->next)
    __specpriv_do_reduction_hh(committed, partial, info);

  return 0;
}

Bool __specpriv_distill_committed_redux_into_main(MappedHeap *committed)
{
  assert( __specpriv_i_am_main_process() );

  ReductionInfo *info = __specpriv_first_reduction_info();
  for(; info; info = info->next)
  {
    void *native_au = info->au;
    void *src_au = heap_translate(native_au, committed);

    __specpriv_do_reduction_pp(src_au, native_au, info);
  }

  return 0;
}







