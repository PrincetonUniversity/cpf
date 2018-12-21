#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "emmintrin.h"
#include "smmintrin.h"

#include "api.h"
#include "internals/affinity.h"
#include "internals/debug.h"
#include "internals/pcb.h"
#include "internals/private.h"
#include "internals/profile.h"
#include "internals/strategy.h"
#include "internals/utils.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/smtx.h"
#include "internals/smtx/malloc.h"
#include "internals/smtx/prediction.h"
#include "internals/smtx/protection.h"
#include "internals/smtx/separation.h"
#include "internals/smtx/units.h"

namespace specpriv_smtx
{

#if PROFILE
static void count_bytes(uint8_t* page, unsigned& m, unsigned& e)
{
  unsigned nonzeros = 0;
  for (unsigned i = 0 ; i < PAGE_SIZE ; i++)
  {
    if (page[i]) m++;
    if (page[i] & 0x02) e++;
  }
}
#endif

// read-only page buffer

// this macro assumes that the size of the pointer is 8 bytes
#define RO_ENTRY_BUFFER_SIZE (PAGE_SIZE / 8)

struct ReadOnlyPageBuffer
{
  void*    data[RO_ENTRY_BUFFER_SIZE];
  unsigned index;
};

ReadOnlyPageBuffer ro_page_buffer;

static cpu_set_t old_affinity;

static bool run_iteration(Wid wid, Iteration iter)
{
  unsigned stage = GET_MY_STAGE(wid);
  Wid      wid_offset = (Wid)iter % GET_REPLICATION_FACTOR(stage);
  Wid      target_wid = GET_FIRST_WID_OF_STAGE(stage) + wid_offset;

  DBG("run_iteration, wid: %u, iter %d, stage %u, target_wid %u\n", wid, iter, stage, target_wid);
  return target_wid == wid;
}

static bool pipeline_fill_iter(Wid wid, Iteration iter)
{
  unsigned stage = GET_MY_STAGE(wid);
  if ((GET_FIRST_WID_OF_STAGE(stage) + iter) < wid)
    return true;
  return false;
}

static void check_misspec(void)
{
  PCB*      pcb = get_pcb();
  Wid       wid = PREFIX(my_worker_id)();
  Iteration iter = __specpriv_current_iter();

  if( pcb->misspeculation_happened )
  {
    if ( pcb->misspeculated_iteration <= iter )
    {
      DBG("check_misspec, wid: %u, misspeculation_happened at %d, I'm at %d. Exit here\n",
          wid, pcb->misspeculated_iteration, iter);
      exit(0);
    }
    else
      DBG("check_misspec, wid: %u, misspeculation_happened at %d, I'm at %d. Keep going\n",
          wid, pcb->misspeculated_iteration, iter);
  }
}

uintptr_t *get_ebp(void)
{
  uintptr_t *r;
  __asm__ volatile ("mov %%rbp, %[r]" : /* output */ [r] "=r" (r));
  return r;
}

unsigned PREFIX(begin_invocation)()
{
  stack_bound = (char*)get_ebp();

  DBG("begin_invocation\n");

  reset_current_iter();

  //
  // set affinity for the main process
  //

  sched_getaffinity(0, sizeof(cpu_set_t), &old_affinity);

  cpu_set_t affinity;
  CPU_ZERO( &affinity );
  CPU_SET( CORE(0), &affinity );
  sched_setaffinity(0, sizeof(cpu_set_t), &affinity );

  //
  // initialize pcb
  //

  PCB *pcb = get_pcb();
  pcb->exit_taken = 0;
  pcb->misspeculation_happened = 0;
  pcb->misspeculated_worker = 0;
  pcb->misspeculated_iteration = -1;
  pcb->misspeculation_reason = 0;
  pcb->last_committed_iteration = -1;

  //
  // returns number of threads to be used
  // TODO: it is possible to have a discrepancy between this number and the
  // number used in __specpriv_spawn_workers_callback. might be good to make
  // them consistent for all cases
  //

  return (unsigned)PREFIX(num_available_workers)();
}

Exit PREFIX(end_invocation)()
{
  Exit exit = get_pcb()->exit_taken;
  destroy_pcb();

  DBG("end_invocation returns %u\n", exit);

  //
  // reset affiinity
  //

  sched_setaffinity(0, sizeof(cpu_set_t), &old_affinity);

  return exit;
}

/*
 * Description:
 *
 *  What worker proceesses do at the beginning of every iteration
 */

void PREFIX(begin_iter)()
{
#if (PROFILE || PROFILE_WEIGHT)
  uint64_t begin = rdtsc();
  uint64_t fit = 0;
#endif

  Wid       wid = PREFIX(my_worker_id)();
  Iteration iter = PREFIX(current_iter)();

  DBG("begin_iteration %d\n", iter);

  //
  // First iteration is special, because it updates loop-invariants values for following iterations.
  // Thus, wait here until the first iteration commits and updates all required iteration
  //

  if (iter)
  {
    // busy waithng

    DBG("good_to_go?\n");


    while (!(*good_to_go)) {
      // At the end of the first iteration, commit prcoess sends some ALLOC packets back to the worker
      // processes to inform all the memory allocation happened during the execution of the first
      // iteration.
      //
      // This is required, because some of these are accessed throughout the loop execution. SLAMP
      // dependence profiling and loop invariant profiling indicates that handling first iteration in
      // special way should be enough. (Needs better explanation...)

      process_reverse_commit_queue(wid);
    }
    DBG("good_to_go!\n");

#if (PROFILE || PROFILE_WEIGHT)
    fit = rdtsc() - begin;
    first_iter_overhead[wid] += fit;
#endif
  }

  //
  // update uncomiited values come from previous stages.
  // This should be happened before misspeculation checking, following stages are executed only when
  // there is no speculation in previous stages.
  //

#if (PROFILE || PROFILE_WEIGHT)
  uint64_t pip = rdtsc();
#endif

  process_incoming_packets( wid, iter );

#if (PROFILE || PROFILE_WEIGHT)
  pip = rdtsc() - pip;
  process_incoming_packet_time[wid] += pip;
#endif

  //
  // check if misspeculation happened from other processes
  //

#if (PROFILE || PROFILE_WEIGHT)
  uint64_t cms = rdtsc();
#endif

  check_misspec();

#if (PROFILE || PROFILE_WEIGHT)
  begin_iter_dominator[wid] += (rdtsc() - cms);
#endif

  //
  // From the second iteration, update loop invariant values and linear predictable values for the
  // worker process at the first stage. Extected values are logged to the buffer when the commit
  // process commits the first iteration. (This is what good_to_go variable for)
  //

  unsigned stage = GET_MY_STAGE( wid ) ;

  if (iter && !stage)
  {
    update_loop_invariants();
    update_linear_predicted_values();


    // sends BOI packet to the try-commit stage here, which makes them to check that the predicted
    // value in the buffer is matched to the actual value

#if PROFILE
    m_outgoing_bw[wid] += ( to_try_commit( wid, (int8_t*)0xDEADBEEF, 0, 0, WRITE, BOI ) * sizeof(packet) );
#else
    to_try_commit( wid, (int8_t*)0xDEADBEEF, 0, 0, WRITE, BOI );
#endif

    DBG("begin_iteration : send BOI packet done\n");
  }

  //
  // Set protection bit of all pre-allocated pages to PROT_NONE so signal handler can know when the
  // pages are touched
  //

#if (PROFILE || PROFILE_WEIGHT)
  uint64_t reset_begin = rdtsc();
#endif

  if (run_iteration(wid, iter))
  {
    reset_protection(wid);
  }

#if (PROFILE || PROFILE_WEIGHT)
  uint64_t reset_time = rdtsc() - reset_begin;
  reset_page_protection_time[wid] += reset_time;

  uint64_t btime = (rdtsc()-begin)-fit;

  if (pipeline_fill_iter(wid, iter))
  {
    btime -= pip;
    process_incoming_packet_time[wid] -= pip;
  }

  begin_iter_time[wid] += btime;
  loop_body_time_buf[wid] = rdtsc();
#endif

  DBG("End of begin_iteration %d\n", iter);
}

/*
 * Utilities for end_iter function
 */

static unsigned clear_read(uint8_t* shadow)
{
  unsigned nonzero = 0;
  bool     readonly = true;

  for (unsigned i = 0 ; i < PAGE_SIZE ; i += 16)
  {
    uint64_t s0 = (*((uint64_t*)(&shadow[i])));
    uint64_t s1 = (*((uint64_t*)(&shadow[i+8])));

    if (s0 == 0 && s1 == 0) continue;
    if (s0) nonzero++;
    if (s1) nonzero++;

    if ((s0 & 0xfbfbfbfbfbfbfbfbL) || (s1 & 0xfbfbfbfbfbfbfbfbL))
      readonly = false;

    // As try_commit unit checks the validity of load with bit 0 only,
    // for read-only bytes whose metadata is b'0100, make it to b'0001,
    // so the commit process verifies its validity
    //
    // For the bytes that written after read, write operation sets the bit 0,
    // thus no need to worry

    uint64_t mask0 = (s0 >> 2) & (~(s0 >> 1)) & 0x0101010101010101L;
    uint64_t mask1 = (s1 >> 2) & (~(s1 >> 1)) & 0x0101010101010101L;

    *((uint64_t*)(&shadow[i])) |= (s0 & mask0);
    *((uint64_t*)(&shadow[i+8])) |= (s1 & mask1);
  }

  if (readonly) nonzero = 0;

  return nonzero;
}

#if PROFILE
static bool is_ro_page(uint8_t* shadow)
{
  for (unsigned i = 0 ; i < PAGE_SIZE ; i += 16)
  {
    uint64_t s0 = (*((uint64_t*)(&shadow[i])));
    uint64_t s1 = (*((uint64_t*)(&shadow[i+8])));

    if (s0 == 0 && s1 == 0) continue;

    if ((s0 & 0xfbfbfbfbfbfbfbfbL) || (s1 & 0xfbfbfbfbfbfbfbfbL))
      return false;
  }
  return true;
}
#endif

static bool check_nrbw(uint8_t* shadow)
{
#if DEBUG_ON
  for (unsigned i = 0 ; i < PAGE_SIZE ; i += 16)
  {
    uint64_t s0 = (*((uint64_t*)(&shadow[i])));
    uint64_t s1 = (*((uint64_t*)(&shadow[i+8])));

    if ( s0 & 0x0101010101010101L )
    {
      DBG("addr %lx shadow %p metadata %lx\n", GET_ORIGINAL_OF(&shadow[i]), &shadow[i], s0);
      return false;
    }
    if ( s1 & 0x0101010101010101L )
    {
      DBG("addr %lx shadow %p metadata %lx\n", GET_ORIGINAL_OF(&shadow[i+8]), &shadow[i+8], s1);
      return false;
    }
  }
#else
  for (unsigned i = 0 ; i < PAGE_SIZE ; i += 16)
  {
    uint64_t s0 = (*((uint64_t*)(&shadow[i])));
    uint64_t s1 = (*((uint64_t*)(&shadow[i+8])));

    if (s0 == 0 && s1 == 0) continue;

    __m128i val = _mm_set_epi32((int)(s1 >> 32), (int)s1, (int)(s0 >> 32), (int)s0);
    __m128i mask = _mm_set1_epi8(1);

    if (!_mm_test_all_zeros(val, mask)) return false;
  }
#endif
  return true;
}

static inline void forward_ro_page(Wid wid, Iteration iter)
{
  if (ro_page_buffer.index != 0)
  {
#if PROFILE
    unsigned packets = forward_page( wid, iter, (void*)(ro_page_buffer.data), CHECK_RO_PAGE );
    m_outgoing_bw[wid] += (packets * sizeof(packet) + sizeof(packet_chunk));
    ro_outgoing_bw[wid] += (packets * sizeof(packet) + sizeof(packet_chunk));
#else
    forward_page( wid, iter, (void*)(ro_page_buffer.data), CHECK_RO_PAGE );
#endif
    memset((void*)(ro_page_buffer.data), 0, PAGE_SIZE);
  }
}

static inline void buffer_ro_page(Wid wid, Iteration iter, void* page)
{
  if (ro_page_buffer.index == RO_ENTRY_BUFFER_SIZE)
  {
    forward_ro_page(wid, iter);
    ro_page_buffer.index = 0;
  }

  (ro_page_buffer.data)[ro_page_buffer.index] = page;
  ro_page_buffer.index += 1;
}

static inline void forward_pair(unsigned nonzero, Wid wid, Iteration iter, void* mem, void* shadow, int16_t check)
{
  if ( nonzero > 8 )
  {
    DBG("forward_page %p\n", mem);

#if PROFILE
    unsigned m = 0, e = 0, packets = 0;

    count_bytes((uint8_t*)shadow, m, e);

    m_total[wid] += m*2;
    e_total[wid] += e*2;
    p_total[wid] += PAGE_SIZE*2;

    packets += forward_page(wid, iter, (void*)mem, check);
    packets += forward_page(wid, iter, (void*)shadow, check);

    m_outgoing_bw[wid] += (m*2 + packets * sizeof(packet));
    e_outgoing_bw[wid] += (e*2 + packets * sizeof(packet));
#else
    forward_page( wid, iter, (void*)mem, check);
    forward_page( wid, iter, (void*)shadow, check);
#endif

    set_zero_page( (uint8_t*)shadow );
  }
  else if ( nonzero )
  {
    DBG("forward_packet %p\n", mem);
    int8_t* m = (int8_t*)mem;
    int8_t* s = (int8_t*)shadow;

    for (unsigned i = 0 ; i < PAGE_SIZE ; i += 8)
    {
      uint64_t* s8 = (uint64_t*)&(s[i]);
      uint64_t* m8 = (uint64_t*)&(m[i]);
      uint64_t  sv = *s8;

      if (sv)
      {
#if PROFILE
        unsigned packets = 0;
        packets += forward_packet( wid, iter, (void*)m8, (void*)*m8, 8, check);
        packets += forward_packet( wid, iter, (void*)NULL, (void*)sv, 8, check);

        m_outgoing_bw[wid] += (packets * sizeof(packet));
        e_outgoing_bw[wid] += (packets * sizeof(packet));
#else
        forward_packet( wid, iter, (void*)m8, (void*)*m8, 8, check);
        forward_packet( wid, iter, (void*)NULL, (void*)sv, 8, check);
#endif
      }
    }

    set_zero_page( (uint8_t*)shadow );
  }
  else
  {
    buffer_ro_page(wid, iter, mem);

    set_zero_page( (uint8_t*)shadow );
  }
}

static void check_region_nrbw(std::set<unsigned>* region, Wid wid, Iteration iter)
{
  if (region)
  {
    for (std::set<unsigned>::iterator i = region->begin() ; i != region->end() ; i++)
    {
      uint64_t begin = heap_begin(*i);
      uint64_t bound = heap_bound(*i);

      DBG("check nrbw from %lx to %lx\n", begin, bound);

      while ( begin < bound )
      {
        uint8_t* shadow = (uint8_t*)GET_SHADOW_OF(begin);

        DBG("check shadow for %lx: %x\n", begin, *shadow);

        if ( *shadow & 0x80 )
        {
          *shadow = *shadow & 0x7f;

          unsigned nonzero = clear_read(shadow);

          if (check_nrbw(shadow))
          {
            forward_pair(nonzero, wid, iter, (void*)begin, (void*)shadow, CHECK_FREE);
          }
          else
          {
            DBG("region %u, nrbw check failed: %lx\n", *i, begin);
            PREFIX(misspec)("nrbw check failed\n");
          }
        }

        begin += PAGE_SIZE;
      }
    }
  }
}

static void check_versioned_region_nrbw(std::set<unsigned>* region, Wid wid, Iteration iter)
{
  if (region)
  {
    for (std::set<unsigned>::iterator i = region->begin() ; i != region->end() ; i++)
    {
      for (unsigned j = 0 ; j < MAX_WORKERS ; j++)
      {
        uint64_t begin = versioned_heap_begin(j, *i);
        uint64_t bound = versioned_heap_bound(j, *i);

        DBG("check versioned nrbw from %lx to %lx\n", begin, bound);

        while ( begin < bound )
        {
          uint8_t* shadow = (uint8_t*)GET_SHADOW_OF(begin);

          DBG("check shadow for %lx: %x\n", begin, *shadow);

          if ( *shadow & 0x80 )
          {
            *shadow = *shadow & 0x7f;

            unsigned nonzero = clear_read(shadow);

            if (check_nrbw(shadow))
            {
              forward_pair(nonzero, wid, iter, (void*)begin, (void*)shadow, CHECK_FREE);
            }
            else
            {
              DBG("region %u, nrbw check failed: %lx\n", *i, begin);
              PREFIX(misspec)("nrbw check failed\n");
            }
          }

          begin += PAGE_SIZE;
        }
      }
    }
  }
}

static void forward_region(std::set<unsigned>* region, Wid wid, Iteration iter, int16_t check)
{
  if (region)
  {
    for (std::set<unsigned>::iterator i = region->begin() ; i != region->end() ; i++)
    {
      uint64_t begin = heap_begin(*i);
      uint64_t bound = heap_bound(*i);

      while ( begin < bound )
      {
        uint8_t* shadow = (uint8_t*)GET_SHADOW_OF(begin);
        if ( *shadow & 0x80 )
        {
          *shadow = *shadow & 0x7f;

          unsigned nonzero = clear_read(shadow);

          forward_pair(nonzero, wid, iter, (void*)begin, (void*)shadow, check);
        }

        begin += PAGE_SIZE;
      }
    }
  }
}

static void forward_versioned_region(std::set<unsigned>* region, Wid wid, Iteration iter, int16_t check)
{
  if (region)
  {
    for (std::set<unsigned>::iterator i = region->begin() ; i != region->end() ; i++)
    {
      for (unsigned j = 0 ; j < MAX_WORKERS ; j++)
      {
        uint64_t begin = versioned_heap_begin(j, *i);
        uint64_t bound = versioned_heap_bound(j, *i);

        while ( begin < bound )
        {
          uint8_t* shadow = (uint8_t*)GET_SHADOW_OF(begin);
          if ( *shadow & 0x80 )
          {
            *shadow = *shadow & 0x7f;

            unsigned nonzero = clear_read(shadow);

            forward_pair(nonzero, wid, iter, (void*)begin, (void*)shadow, check);
          }

          begin += PAGE_SIZE;
        }
      }
    }
  }
}

/*
 * Description:
 *
 *   What worker processes does at the end of every iteration
 */

void PREFIX(end_iter)(void)
{
  Wid       wid = PREFIX(my_worker_id)();
  Iteration iter = PREFIX(current_iter)();
  unsigned  stage = GET_MY_STAGE( wid ) ;

  DBG("end_iteration, %d\n", iter);

#if (PROFILE || PROFILE_WEIGHT)
  uint64_t begin = rdtsc();
  loop_body_time[wid] += begin-loop_body_time_buf[wid];
#endif

  if ( run_iteration(wid, iter) )
  {
    //
    // First, update metadata for values that predicted by loop-invariant/linear prediction. Clear the
    // 'Read Before Write' bit for these values, so they don't trigger misspeculation from the
    // try-commit stage. There are separate mechanism for verify those values
    //

    update_shadow_for_loop_invariants();
    update_shadow_for_linear_predicted_values();

    // If there are ver_mallocs that not broadcasted yet, handle them first

    VerMallocBuffer* buf = &(ver_malloc_buffer[wid]);
    broadcast_malloc_chunk(wid, (int8_t*)(buf->elem), buf->index * sizeof(VerMallocInstance));
    memset(buf->elem, 0, sizeof(VerMallocInstance) * PAGE_SIZE);
    buf->index = 0;

    //
    // Forward pages for global varialabes that have ever touched during the execution
    // (thus have metadata set)
    //

#if PROFILE
    unsigned tp = 0;
    unsigned wp = 0;
#endif

    for (size_t i=0,e=shadow_globals->size() ; i<e ; i++)
    {

      // sot: optimize globals. Do similar opts to heaps pages. If less than 64 bytes then do not send the whole page.
      /*
      uint8_t* shadow_page = (*shadow_globals)[i];
      if ( !is_zero_page(shadow_page) )
      {
#if PROFILE
        tp += 1;
        if (!is_ro_page(shadow_page)) wp += 1;

        unsigned m = 0, e = 0, packets = 0;

        count_bytes(shadow_page, m, e);

        m_total[wid] += m*2;
        e_total[wid] += e*2;
        p_total[wid] += PAGE_SIZE*2;

        packets += forward_page( wid, iter, (void*)GET_ORIGINAL_OF(shadow_page), CHECK_REQUIRED );
        packets += forward_page( wid, iter, (void*)(shadow_page), CHECK_REQUIRED );

        m_outgoing_bw[wid] += (m + packets * sizeof(packet));
        e_outgoing_bw[wid] += (e + packets * sizeof(packet));
#else
        forward_page( wid, iter, (void*)GET_ORIGINAL_OF(shadow_page), CHECK_REQUIRED );
        forward_page( wid, iter, (void*)(shadow_page), CHECK_REQUIRED );
#endif

        DBG("forward_page: %p\n", GET_ORIGINAL_OF(shadow_page));
        set_zero_page( shadow_page );
      }
      */

      uint8_t* shadow = (*shadow_globals)[i];

      // sot: not sure if touched can be used here
      // maybe the mapping of globals is different from heaps
      // TODO: need to figure out if I can do 0x80 and &0x7f
      bool     touched = (*shadow) & 0x80;

      if (touched)
      {
        // sot: TODO: maybe this is not needed. Used for heaps, not for globals
        *shadow = *shadow & 0x7f;

        if (is_zero_page(shadow)) continue;

#if PROFILE
        tp += 1;
        if (!is_ro_page(shadow)) wp += 1;
#endif

        unsigned nonzero = clear_read(shadow);

        forward_pair(nonzero, wid, iter, (void*)GET_ORIGINAL_OF(shadow), (void*)shadow, CHECK_REQUIRED);
      }

    }

    //
    // Forward pages for stack that have ever touched during the execution
    // (thus have metadata set)
    // TODO: still unclear why we should send stack pages
    //

    for (size_t i=0,e=shadow_stacks->size() ; i<e ; i++)
    {

      // sot: optimize stacks. Do similar opts to heaps pages. If less than 64 bytes then do not send the whole page.
      /*
      uint8_t* shadow_page = (*shadow_stacks)[i];
      if ( !is_zero_page(shadow_page) )
      {
#if PROFILE
        tp += 1;
        if (!is_ro_page(shadow_page)) wp += 1;

        unsigned m = 0, e = 0, packets = 0;

        count_bytes(shadow_page, m, e);

        m_total[wid] += m*2;
        e_total[wid] += e*2;
        p_total[wid] += PAGE_SIZE*2;

        packets += forward_page( wid, iter, (void*)GET_ORIGINAL_OF(shadow_page), CHECK_REQUIRED );
        packets += forward_page( wid, iter, (void*)(shadow_page), CHECK_REQUIRED );

        m_outgoing_bw[wid] += (m + packets * sizeof(packet));
        e_outgoing_bw[wid] += (e + packets * sizeof(packet));
#else
        forward_page( wid, iter, (void*)GET_ORIGINAL_OF(shadow_page), CHECK_REQUIRED );
        forward_page( wid, iter, (void*)(shadow_page), CHECK_REQUIRED );
#endif

        DBG("forward_page: %p\n", GET_ORIGINAL_OF(shadow_page));
        set_zero_page( shadow_page );
      }
      */

      uint8_t* shadow = (*shadow_stacks)[i];

      // sot: not sure if touched can be used here
      // maybe the mapping of stacks is different from heaps
      // TODO: need to figure out if I can do 0x80 and &0x7f
      bool     touched = (*shadow) & 0x80;

      if (touched)
      {
        // sot: TODO: maybe this is not needed. Used for heaps, not for stacks
        *shadow = *shadow & 0x7f;

        if (is_zero_page(shadow)) continue;

#if PROFILE
        tp += 1;
        if (!is_ro_page(shadow)) wp += 1;
#endif

        unsigned nonzero = clear_read(shadow);

        forward_pair(nonzero, wid, iter, (void*)GET_ORIGINAL_OF(shadow), (void*)shadow, CHECK_REQUIRED);
      }

    }

    //
    // Forward pages for dynamically allocated memories that have ever thouched during the execution.
    // (thus have it's shadow bit set)
    // Those pages might be allocated during the execution of the parallel region.
    //

#if (PROFILE || PROFILE_WEIGHT)
    uint64_t eid = rdtsc();
#endif

    for (std::set<uint8_t*>::iterator i=shadow_heaps->begin(), e=shadow_heaps->end() ; i != e ; i++)
    {
#if (PROFILE || PROFILE_WEIGHT)
      heap_pages[wid] += 1;
#endif

      uint8_t* shadow = *i;
      bool     touched = (*shadow) & 0x80;

      if (touched)
      {
#if (PROFILE || PROFILE_WEIGHT)
        outgoing_heaps[wid] += 1;
#endif
        *shadow = *shadow & 0x7f;

        if (is_zero_page(shadow)) continue;

        unsigned nonzero = clear_read(shadow);

#if PROFILE
        tp += 1;
        if (nonzero) wp += 1;
#endif

        forward_pair(nonzero, wid, iter, (void*)GET_ORIGINAL_OF(shadow), (void*)shadow, CHECK_REQUIRED);
      }
    }
    forward_ro_page(wid, iter);

#if PROFILE
    PROFDUMP("iter: %u, touched: %u written: %u\n", iter, tp, wp);
#endif

#if (PROFILE || PROFILE_WEIGHT)
    eid = rdtsc() - eid;
    send_outgoing_heap_time[wid] += eid;
    end_iter_dominator[wid] += eid;
#endif

    //
    // Forward 'unclassified' heaps
    //

#if 0 // separation not supported
    forward_region(get_uc(), wid, iter, CHECK_REQUIRED);
    forward_versioned_region(get_versioned_uc(), wid, iter, CHECK_REQUIRED);
#endif

    //
    // Check NRBW(No-Read-Before-Write) heaps. If the violate the property, set misspec. Otherwise,
    // forward with CHECK_FREE flag.
    //

#if 0 // separation not supported
    DBG("check NRBW region\n");
    check_region_nrbw(get_nrbw(), wid, iter);

    DBG("check versioned NRBW region\n");
    check_versioned_region_nrbw(get_versioned_nrbw(), wid, iter);

    //
    // If the worker is for the parallel stage, check NRBW property. Otherwise, just forward pages
    // with CHECK_FREE flag.
    //

    unsigned  repfac = GET_REPLICATION_FACTOR(stage);
    if ( repfac > 1)
    {
      DBG("check STAGE PRIVATE region\n");
      check_region_nrbw(get_stage_private(stage), wid, iter);

      DBG("check versioned PRIVATE region\n");
      check_versioned_region_nrbw(get_versioned_stage_private(stage), wid, iter);
    }
    else
    {
      forward_region(get_stage_private(stage), wid, iter, CHECK_FREE);
      forward_versioned_region(get_versioned_stage_private(stage), wid, iter, CHECK_FREE);
    }
#endif
  }

  //
  // If I'm the worker process who is responsible for current iteration,
  // send EOI packet to following stages. 'wid_offset' computation is to find the worker process
  // responsible for the iteration 'iter'. Compare it with my wid to check if I'm the one.
  //

  Wid wid_offset = GET_WID_OFFSET_IN_STAGE( (Wid)iter, stage );

  if ( ( wid - GET_FIRST_WID_OF_STAGE( stage ) ) == wid_offset )
  {
#if PROFILE
    m_outgoing_bw[wid] += (sizeof(packet) * broadcast_event( wid, (int8_t*)0xDEADBEEF, 0, NULL, WRITE, EOI ));
#else
    broadcast_event( wid, (int8_t*)0xDEADBEEF, 0, NULL, WRITE, EOI );
#endif
  }

  DBG("End of end_iteration, %d\n", iter);

  // increase iteration count

  advance_iter();

#if (PROFILE || PROFILE_WEIGHT)
  end_iter_time[wid] += (rdtsc()-begin);
#endif
}

}
