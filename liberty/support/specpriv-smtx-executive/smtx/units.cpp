#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "mmintrin.h"
#include "emmintrin.h"
#include "smmintrin.h"
#include "xmmintrin.h"

#include "api.h"
#include "internals/constants.h"
#include "internals/control.h"
#include "internals/debug.h"
#include "internals/pcb.h"
#include "internals/private.h"
#include "internals/profile.h"
#include "internals/strategy.h"
#include "internals/utils.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/malloc.h"
#include "internals/smtx/prediction.h"
#include "internals/smtx/separation.h"
#include "internals/smtx/smtx.h"

namespace specpriv_smtx
{

#if PROFILE
static void profile_packet(int8_t* shadow, size_t size, unsigned& m, unsigned& e)
{
  for (unsigned i = 0 ; i < size ; i++)
  {
    if (shadow[i]) m++;
    if (shadow[i] & 0x02) e++;
  }
}
#endif

/*
 * update page if the shadow bit is set
 */

static void update_page(int8_t* addr, int8_t* data, int8_t* shadow)
{
#if !(DEBUG_ON)
  for (unsigned i = 0 ; i < PAGE_SIZE ; i += 16)
  {
    uint64_t m0 = (*((uint64_t*)(&shadow[i]))) << 6;
    uint64_t m1 = (*((uint64_t*)(&shadow[i+8]))) << 6;
    uint64_t d0 = (*((uint64_t*)(&data[i])));
    uint64_t d1 = (*((uint64_t*)(&data[i+8])));

    __m128i m = _mm_set_epi32((int)(m1 >> 32), (int)m1, (int)(m0 >> 32), (int)m0);
    __m128i d = _mm_set_epi32((int)(d1 >> 32), (int)d1, (int)(d0 >> 32), (int)d0);
    _mm_maskmoveu_si128(d, m, (char*)&(addr[i]));
  }
#else
  for (unsigned i = 0 ; i < PAGE_SIZE ; i++)
  {
    if ( shadow[i] & 0x2 )
    {
      DBG("update_page [%d] %p: %x->%x\n", PREFIX(current_iter)(), &addr[i], (uint8_t)addr[i], (uint8_t)data[i]);
      addr[i] = data[i];
    }
  }
#endif
}

static inline void update_super_packet(unsigned stage, packet* p, packet* shadow_p)
{
  int8_t*       addr = (int8_t*)p->ptr;
  packet_chunk* mem_chunk = (packet_chunk*)(p->value);
  packet_chunk* shadow_chunk = (packet_chunk*)(shadow_p->value);

  update_page(addr, mem_chunk->data, shadow_chunk->data);

  // mark chunk read
  mark_packet_chunk_read(stage, mem_chunk);
  mark_packet_chunk_read(stage, shadow_chunk);

  // mark shadow packet read
  shadow_p->ptr = NULL;
}

static inline void update_normal_packet(packet* p, packet* shadow_p)
{
  DBG("addr: %p value: %p shadow: %p\n", p->ptr, p->value, shadow_p->value);
  int64_t shadow = (int64_t)(shadow_p->value);
#if 0
  __m64   mask = _mm_cvtsi64_m64(shadow << 6);

  int64_t value = (int64_t)(p->value);

  _mm_maskmove_si64(_mm_cvtsi64_m64(value), mask, (char*)(p->ptr));
#endif

  char* ptr = (char*)(p->ptr);
  int64_t value = (int64_t)(p->value);

  for (unsigned i = 0 ; i < 8 ; i++)
  {
    if ( shadow & 0x2 )
      ptr[i] = value & 0xff;
    shadow >>= 8;
    value >>= 8;
  }
}

/*
 * This function is for worker processes.
 * Worker process calls this function from the begin_iteration function to update the information
 * from the following processes
 */

void process_reverse_commit_queue(Wid wid)
{
  queue_t* queue = reverse_commit_queues[wid];

  while ( !__sw_queue_empty( queue ) )
  {
    DBG("process incoming packets from reverse_commit_queues, wid %u\n", wid);

    packet* p = (packet*)__sw_queue_consume( queue );

    // At this moment, ALLOC is the only valid packet type here.
    // Update memory allocation for the memory allocations happened in the other worker processes,
    // which handle same or later stages than mine.

    if (p->is_write == REGULAR)
    {
      // For REGULAR type ALLOC packet, p->value is the source id.

      Wid source_wid = (Wid)((uint64_t)p->value);

      unsigned src_wid_stage = GET_MY_STAGE(source_wid);
      unsigned my_stage = GET_MY_STAGE(wid);

      if ( (src_wid_stage >= my_stage) && (source_wid != wid) )
      {
        update_ver_malloc( source_wid, p->size, p->ptr );
      }
    }
    else
    {
      assert( p->is_write == SEPARATION );

      // For SEPARATION type ALLOC packet, p->value is a pack of heapid and source wid

      Wid      source_wid = (Wid)( (uint64_t)(p->value) >> 32 );
      unsigned heapid = (unsigned)((uint64_t)(p->value));

      unsigned src_wid_stage = GET_MY_STAGE(source_wid);
      unsigned my_stage = GET_MY_STAGE(wid);

      if ( (src_wid_stage >= my_stage) && (source_wid != wid) )
      {
        update_ver_separation_malloc( p->size, source_wid, heapid, p->ptr );
      }
    }
  }
}

void process_incoming_packets(Wid wid, Iteration iter)
{
  unsigned my_stage = GET_MY_STAGE(wid);
  unsigned i;

  assert( wid < try_commit_begin );

  for (i = 0 ; i < my_stage ; i++)
  {
    Wid wid_offset = (Wid)iter % GET_REPLICATION_FACTOR(i);
    Wid source_wid = GET_FIRST_WID_OF_STAGE(i) + wid_offset;

    DBG("process_incoming_packets, source_wid: %u, source_stage: %u, my_wid: %u, my_stage: %u\n", source_wid, i, wid, my_stage);

    bool done = false;

    while( !done )
    {
      packet* p = (packet*)__sw_queue_consume( ucvf_queues[source_wid][wid] );
      DUMPPACKET("\t", p);

#if PROFILE
      incoming_bw[wid] += sizeof(packet);
#endif

      switch(p->type)
      {
        case NORMAL:
        {
          // packet comes as a pair
          packet* shadow_p = (packet*)__sw_queue_consume( ucvf_queues[source_wid][wid] );
          update_normal_packet(p, shadow_p);

#if PROFILE
          incoming_bw[wid] += sizeof(packet);
          m_incoming_bw[wid] += (2 * sizeof(packet));
          e_incoming_bw[wid] += (2 * sizeof(packet));
#endif

          break;
        }
        case SUPER:
        {
#if PROFILE
          incoming_bw[wid] += sizeof(packet_chunk);
#endif
          if (p->is_write == CHECK_RO_PAGE)
          {
#if (PROFILE || PROFILE_WEIGHT)
            incoming_pages[wid] += 1;
#endif
            // basically do nothing
            packet_chunk* mem_chunk = (packet_chunk*)(p->value);
            mark_packet_chunk_read(my_stage, mem_chunk);
          }
          else
          {
#if (PROFILE || PROFILE_WEIGHT)
            incoming_pages[wid] += 2;
#endif
            // packet comes as a pair
            packet* shadow_p = (packet*)__sw_queue_consume( ucvf_queues[source_wid][wid] );
#if PROFILE
            incoming_bw[wid] += sizeof(packet);
            incoming_bw[wid] += sizeof(packet_chunk);

            unsigned m = 0, e = 0;

            packet_chunk* shadow_chunk = (packet_chunk*)(shadow_p->value);
            profile_packet((int8_t*)(shadow_chunk->data), PAGE_SIZE, m, e);

            m_incoming_bw[wid] += (m + sizeof(packet))*2;
            e_incoming_bw[wid] += (e + sizeof(packet))*2;
#endif
            update_super_packet(my_stage, p, shadow_p);
          }
          break;
        }
        case EOI:
        case MISSPEC:
        case EOW:
          done = true;
          break;
        case ALLOC:
        {
#if (PROFILE || PROFILE_WEIGHT)
          incoming_pages[wid] += 1;
#endif
          if (p->size == 0)
          {
#if PROFILE
            incoming_bw[wid] += sizeof(packet_chunk);
            m_incoming_bw[wid] += sizeof(packet_chunk);
            e_incoming_bw[wid] += sizeof(packet_chunk);
            alloc_incoming_bw[wid] += sizeof(packet_chunk);
#endif
            unsigned valids = 0;
            packet_chunk*      chunk = (packet_chunk*)(p->value);
            VerMallocInstance* vmi = (VerMallocInstance*)(chunk->data);
            for (unsigned k = 0 ; k < PAGE_SIZE / sizeof(VerMallocInstance) ; k++)
            {
              if (!vmi->ptr) break;

              if (vmi->heap == -1)
              {
                update_ver_malloc( source_wid, vmi->size, (void*)vmi->ptr );
              }
              else
              {
                update_ver_separation_malloc( vmi->size, source_wid, (unsigned)(uint64_t)(vmi->heap), (void*)vmi->ptr );
              }

              valids++;
              vmi++;
            }
            mark_packet_chunk_read(my_stage, chunk);

            break;
          }
          break;
        }
        case FREE:
          update_ver_free( source_wid, p->ptr );
          break;
        case BOI:
          // nothing to do
          break;
        default:
          assert( false && "Unsupported type of packet" );
      }

      // mark packet consumed
      p->ptr = NULL;
    }
  }
}

/*
 * Description:
 *
 *  Try-commit unit forwards buffered packets to the commit unit, which are now safe to be committed
 *
 * Arguments:
 *
 *  wid - worker id. Should be one of the try_commit
 *  buf - a packet buffer
 *  size - size of the packet buffer
 */

static void forward_values_to_commit(Wid wid, packet* buf, unsigned size)
{
  DBG("\tforward_values_to_commit\n");

#if PROFILE
  uint64_t b = rdtsc();
#endif

  assert( wid >= try_commit_begin );
  queue_t* commit_queue = commit_queues[wid-try_commit_begin];

  unsigned i = 0;
  for ( ; i < size ; i++)
  {
    packet* p = &buf[i];

    DBG("\tbuffered packet %u\n", i);
    //DUMPPACKET("\t", p);

    packet* new_p = create_packet(wid, p->ptr, p->value, p->size, p->is_write, p->type);

#if PROFILE
    outgoing_bw[wid] += sizeof(packet);
#endif

    DBG("\tnew packet %u, commit_queue headptr: %p tailptr: %p, headval: %p, tailval: %p\n", i, commit_queue->head,
        commit_queue->tail, *(commit_queue->head), *(commit_queue->tail));

    __sw_queue_produce( commit_queue, (void*)new_p );
  }

#if PROFILE
  globals_time[wid] += (rdtsc() - b);
#endif

}

/*
 * Description:
 *
 *  Temporarily store the packet into a buffer
 *
 * Arguments:
 *
 *  buf - a packet buffer
 *  index - an index of the buffer
 *  p - a packet to be stored
 */

static unsigned buffer_packet(packet* buf, unsigned buf_index, packet* p)
{
  //assert( buf_index < PACKET_POOL_SIZE && "packet buffer overflowed" );

  if ( buf_index == PACKET_POOL_SIZE )
  {
    forward_values_to_commit( PREFIX(my_worker_id)(), buf, PACKET_POOL_SIZE );
    buf_index = 0;
  }

  buf[buf_index].ptr = p->ptr;
  buf[buf_index].value = p->value;
  buf[buf_index].size = p->size;
  buf[buf_index].is_write = p->is_write;
  buf[buf_index].type = p->type;
  buf[buf_index].sign = p->sign;

  DBG("\tbuf_packet, index %u\n", buf_index);

  return buf_index + 1;
}

static bool check_packet(int8_t* addr, uint64_t data, uint64_t shadow, size_t size, unsigned stage)
{
  // increase stage id by 1 to distinguish from uninitialized 0

  uint8_t new_stage_id = (uint8_t)(stage+1);

  // it violates assumption if
  // - stage is a sequential stage, and the metadata is greater than the stage
  // - stage is a parallel stage, and the metadata is greater than or equals to the stage

  uint8_t ref = (uint8_t)new_stage_id;

  if ( GET_REPLICATION_FACTOR(stage) == 1 ) // sequential stage
    ref += 1;

  assert(size == 8);

  for (unsigned i = 0 ; i < size ; i++)
  {
    if ( shadow & 0x1 )
    {
      // verify

      uint8_t* localshadow = (uint8_t*)GET_SHADOW_OF( &(addr[i]) );
      if ( *localshadow >= ref )
      {
        DBG("[%d,%u] try_commit misspec, %p: %x, shadow %u\n", PREFIX(current_iter)(), new_stage_id, localshadow, *localshadow, shadow & 0xff);
        return false;
      }
    }
    if ( shadow & 0x2 )
    {
      addr[i] = data & 0xff;
      uint8_t* localshadow = (uint8_t*)GET_SHADOW_OF( &(addr[i]) );
      *localshadow = (uint8_t)(new_stage_id);
    }

    shadow >>= 8;
    data   >>= 8;
  }

  return true;
}

static bool check_ro_page(int8_t* addr, unsigned stage)
{
  // increase stage id by 1 to distinguish from uninitialized 0

  uint8_t new_stage_id = (uint8_t)(stage+1);

  // it violates assumption if
  // - stage is a sequential stage, and the metadata is greater than the stage
  // - stage is a parallel stage, and the metadata is greater than or equals to the stage

  uint8_t ref = (uint8_t)new_stage_id;

  if ( GET_REPLICATION_FACTOR(stage) == 1 ) // sequential stage
    ref += 1;

  for (unsigned i = 0 ; i < PAGE_SIZE ; i++)
  {
    // verify

    uint8_t* localshadow = (uint8_t*)GET_SHADOW_OF( &(addr[i]) );
    if ( *localshadow >= ref )
    {
      DBG("[%d,%u] try_commit RO misspec, %p: %xn", PREFIX(current_iter)(), new_stage_id, localshadow, *localshadow);
      return false;
    }
  }
}

static bool check_page(int8_t* addr, int8_t* data, int8_t* shadow, unsigned stage)
{
#if PROFILE
  //uint64_t b = rdtsc();
#endif

  // increase stage id by 1 to distinguish from uninitialized 0

  uint8_t new_stage_id = (uint8_t)(stage+1);

  // it violates assumption if
  // - stage is a sequential stage, and the metadata is greater than the stage
  // - stage is a parallel stage, and the metadata is greater than or equals to the stage

  uint8_t ref = (uint8_t)new_stage_id;

  if ( GET_REPLICATION_FACTOR(stage) == 1 ) // sequential stage
    ref += 1;
#if 1
  for (unsigned i = 0 ; i < PAGE_SIZE ; i += 16)
  {
    uint64_t m[2];
    m[0] = (*((uint64_t*)(&shadow[i])));
    m[1] = (*((uint64_t*)(&shadow[i+8])));

    if (m[0] == 0 && m[1] == 0)
    {
      continue;
    }

    // check load

    uint64_t m00 = m[0] << 7;
    uint64_t m01 = m[1] << 7;
    __m128i  mask0 = _mm_set_epi32((int)(m01 >> 32), (int)m01, int(m00 >> 32), int(m00));

    uint64_t l0 = (*((uint64_t*)( GET_SHADOW_OF(&addr[i]) )));
    uint64_t l1 = (*((uint64_t*)( GET_SHADOW_OF(&addr[i+8]) )));

    // sub localshadow from ref: if MSB is 1, ref is greater than the local shadow

    __m128i ls = _mm_set_epi32((int)(l1 >> 32), (int)l1, (int)(l0 >> 32), (int)l0);
    __m128i r = _mm_set1_epi8(ref);
    __m128i mask1 = _mm_sub_epi8(ls, r);

    __m128i mask = _mm_andnot_si128(mask1, mask0);
    __m128i msbmask = _mm_set1_epi8((char)0x80);

    if (!_mm_test_all_zeros(mask, msbmask))
    {
      return false;
    }

    // update

    uint64_t m20 = m[0] << 6;
    uint64_t m21 = m[1] << 6;
    __m128i  mask20 = _mm_set_epi32((int)(m21 >> 32), (int)m21, (int)(m20 >> 32), (int)m20);

    uint64_t d[2];
    d[0] = (*((uint64_t*)(&data[i])));
    d[1] = (*((uint64_t*)(&data[i+8])));

    __m128i ds = _mm_set_epi32((int)(d[1] >> 32), (int)d[1], (int)(d[0] >> 32), (int)d[0]);
    _mm_maskmoveu_si128(ds, mask20, (char*)&(addr[i]));

    __m128i si = _mm_set1_epi8(new_stage_id);
    _mm_maskmoveu_si128(si, mask20, (char*)GET_SHADOW_OF( &addr[i] ));
  }
#endif
#if 0
  unsigned check = 0;
  unsigned write = 0;
  for (unsigned i = 0 ; i < PAGE_SIZE ; i++)
  {
    if ( shadow[i] & 0x1 )
    {
      check += 1;
      // verify

      uint8_t* localshadow = (uint8_t*)GET_SHADOW_OF( &(addr[i]) );
      if ( *localshadow >= ref )
      {
        DBG("[%d,%u] try_commit misspec, %p: %x, shadow %u\n", PREFIX(current_iter)(), new_stage_id, localshadow, *localshadow, shadow[i]);
        return false;
      }
    }
    if ( shadow[i] & 0x2 )
    {
      write += 1;
      // update

      addr[i] = data[i];
      uint8_t* localshadow = (uint8_t*)GET_SHADOW_OF( &(addr[i]) );
      *localshadow = (uint8_t)(new_stage_id);
      //DBG("[%d,%u] %p: %x, shadow %u\n", PREFIX(current_iter)(), new_stage_id, localshadow, new_stage_id, shadow[i]);
    }
  }

  //PROFDUMP("check %u write %u data %u\n", check, write, PAGE_SIZE);
#endif
#if PROFILE
  //verification_stall_time[ PREFIX(my_worker_id)() ] += ( rdtsc() - b );
#endif
  return true;
}

void try_commit()
{
  // separate packet buffer for try_commit unit

  packet* try_commit_buffer = (packet*)malloc( sizeof(packet) * PACKET_POOL_SIZE );

  unsigned num_stages = GET_NUM_STAGES();

  Wid wid = PREFIX(my_worker_id)();
  Wid my_try_commit_id = wid - try_commit_begin;

  while ( true )
  {
    // for each 'iter'

    unsigned  buf_index = 0;
    Iteration iter = PREFIX(current_iter)();
    Iteration terminated_iter = -1;

    unsigned i;
    for (i = 0 ; i < num_stages ; i++)
    {
      // find id of a worker that handles 'iter' of stage 'i'

      Wid wid_offset = (Wid)iter % GET_REPLICATION_FACTOR(i);
      Wid source_wid = GET_FIRST_WID_OF_STAGE(i) + wid_offset;

      DBG("ver_try_commit, source_wid: %u, source_stage: %u\n", source_wid, i);

      bool done = false;
      bool misspec = false;

      while( !done && !misspec )
      {
#if PROFILE
        uint64_t while_begin = rdtsc();
#endif

        packet* p = (packet*)__sw_queue_consume( queues[source_wid][my_try_commit_id] );
        DUMPPACKET("[try_commit] ", p);

#if PROFILE
        uint64_t diff = (rdtsc() - while_begin);
        begin_iter_dominator[wid] += diff;
#endif

#if PROFILE
        incoming_bw[wid] += sizeof(packet);
        uint64_t work_begin = rdtsc();
#endif

        switch (p->type)
        {
          case NORMAL:
          {
            // packet comes as a pair
            packet* shadow_p = (packet*)__sw_queue_consume( queues[source_wid][my_try_commit_id] );

#if PROFILE
            incoming_bw[wid] += sizeof(packet);
            m_incoming_bw[wid] += (2 * sizeof(packet));
            e_incoming_bw[wid] += (2 * sizeof(packet));
#endif

            int8_t*  addr = (int8_t*)p->ptr;
            uint64_t data = (uint64_t)p->value;
            uint64_t shadow = (uint64_t)shadow_p->value;

            if (p->is_write == CHECK_REQUIRED)
            {
              if ( !check_packet(addr, data, shadow, p->size, i) )
              {
                PREFIX(misspec)("try-commit failed\n");
                assert( false && "shouldn't be reached\n" );
              }
            }
#if 0 // enable if region separation enabled from loopevent.cpp
            else
            {
              assert( false );
              try_commit_update_loop_invariants(addr, (int8_t*)&data, (int8_t*)&shadow, p->size);
              try_commit_update_linear_predicted_values(addr, (int8_t*)&data, (int8_t*)&shadow, p->size);
            }
#endif

            // buffer packets

            buf_index = buffer_packet(try_commit_buffer, buf_index, p);
            buf_index = buffer_packet(try_commit_buffer, buf_index, shadow_p);

            // mark shadow_packet read
            shadow_p->ptr = NULL;

            break;
          }
          case SUPER:
          {
            if (p->is_write == CHECK_RO_PAGE)
            {
#if PROFILE
              incoming_bw[wid] += sizeof(packet_chunk);
              m_incoming_bw[wid] += sizeof(packet_chunk);
              ro_incoming_bw[wid] += sizeof(packet_chunk);

              read_only_chunks[wid] += 1;
#endif
              packet_chunk* mem_chunk = (packet_chunk*)(p->value);
              void**        pages = (void**)(mem_chunk->data);

              for (unsigned pageindex = 0 ; pageindex < (PAGE_SIZE / 8) ; pageindex++)
              {
#if PROFILE
                read_only_pages[wid] += 1;
#endif
                check_ro_page((int8_t*)(pages[pageindex]), i);
              }

              // do not need to pass this chunk to the commit stage
              mark_packet_chunk_read(0, mem_chunk);
            }
            else
            {
              // packet comes as a pair
              packet* shadow_p = (packet*)__sw_queue_consume( queues[source_wid][my_try_commit_id] );
#if PROFILE
              incoming_bw[wid] += sizeof(packet);
              incoming_bw[wid] += sizeof(packet_chunk);
              incoming_bw[wid] += sizeof(packet_chunk);
#endif
              int8_t*       addr = (int8_t*)p->ptr;
              packet_chunk* mem_chunk = (packet_chunk*)(p->value);
              packet_chunk* shadow_chunk = (packet_chunk*)(shadow_p->value);

              if (p->is_write == CHECK_REQUIRED)
              {
#if PROFILE
                unsigned m = 0, e = 0;

                packet_chunk* shadow_chunk = (packet_chunk*)(shadow_p->value);
                profile_packet((int8_t*)(shadow_chunk->data), PAGE_SIZE, m, e);

                m_incoming_bw[wid] += (m + sizeof(packet))*2;
                e_incoming_bw[wid] += (e + sizeof(packet))*2;
#endif
                if ( !check_page(addr, mem_chunk->data, shadow_chunk->data, i) )
                {
                  PREFIX(misspec)("try-commit failed\n");
                  assert( false && "shouldn't be reached\n" );
                }
              }
#if 0 // enable if region separation enabled from loopevent.cpp
              else
              {
                assert( false );
                try_commit_update_loop_invariants(addr, mem_chunk->data, shadow_chunk->data, PAGE_SIZE);
                try_commit_update_linear_predicted_values(addr, mem_chunk->data, shadow_chunk->data, PAGE_SIZE);
              }
#endif

              // buffer packets

              buf_index = buffer_packet(try_commit_buffer, buf_index, p);
              buf_index = buffer_packet(try_commit_buffer, buf_index, shadow_p);

              // mark shadow_packet read
              shadow_p->ptr = NULL;
            }
            break;
          }
          case BOI:
          {
#if PROFILE
            m_incoming_bw[wid] += sizeof(packet);
#endif
            /*
             * loop invariant predictions and linear predictions are verified here, not in case EOI:
             * because EOI packet is received at each subTX boundary, not only at TX boundary.
             * In other words, there are multiple EOI packets sent within a single iteration.
             *
             * In contrast, BOI packet is sent only once per iteration at the beginning.
             * Therefore, verifying loop invariants and linear predictions here at this point is
             * effectively same as verifying them at the end of the previous iteration.
             */

            if ( !verify_loop_invariants() )
            {
              DBG("loop invariant buffer verification failed\n");
              PREFIX(misspec)("try-commit failed\n");
              assert( false && "shouldn't be reached\n" );
            }
            if ( !verify_linear_predicted_values() )
            {
              DBG("linear prediction verification failed\n");
              PREFIX(misspec)("try-commit failed\n");
              assert( false && "shouldn't be reached\n" );
            }

            break;
          }

          case EOI:
          {
#if PROFILE
            m_incoming_bw[wid] += sizeof(packet);
#endif

            done = true;
            buf_index = buffer_packet(try_commit_buffer, buf_index, p);

            break;
          }
          case MISSPEC:
            misspec = true;
            break;

          case EOW:
          {
            DBG("try-commit handles EOW packet\n");

            done = true;

            if (terminated_iter == -1)
            {
              terminated_iter = iter;
            }
            else
            {
              assert( terminated_iter == iter && "different stages terminated at different iterations" );
            }

            if (i == num_stages-1)
            {
              buf_index = buffer_packet(try_commit_buffer, buf_index, p);
            }

            break;
          }
          case ALLOC:
          {
            DBG("try-commit handles ALLOC packet, %p\n", p->ptr);

            if (my_try_commit_id == 0)
            {
              buf_index = buffer_packet(try_commit_buffer, buf_index, p);
            }
            if (p->size == 0) // chunk of mallocs
            {
#if PROFILE
              incoming_bw[wid] += sizeof(packet_chunk);
              m_incoming_bw[wid] += sizeof(packet_chunk);
              e_incoming_bw[wid] += sizeof(packet_chunk);
              alloc_incoming_bw[wid] += sizeof(packet_chunk);
#endif
              unsigned valids = 0;
              packet_chunk*      chunk = (packet_chunk*)(p->value);
              VerMallocInstance* vmi = (VerMallocInstance*)(chunk->data);
              for (unsigned k = 0 ; k < PAGE_SIZE / sizeof(VerMallocInstance) ; k++)
              {
                if (!vmi->ptr) break;

                if (vmi->heap == -1)
                {
                  update_ver_malloc( source_wid, vmi->size, (void*)vmi->ptr );
                }
                else
                {
                  update_ver_separation_malloc( vmi->size, source_wid, (unsigned)(uint64_t)(vmi->heap), (void*)vmi->ptr );
                }

                valids++;
                vmi++;
              }
              // mark_packet_chunk_read(0, chunk);

              break;
            }
            break;
          }
          case FREE:
            if (my_try_commit_id == 0)
            {
              buf_index = buffer_packet(try_commit_buffer, buf_index, p);
            }

            update_ver_free( source_wid, p->ptr );
            break;

          default:
            assert( false && "Unsupported type of packet" );
        }

        // mark the packet consumed
        p->ptr = NULL;
        DBG("\tpacket marked available, %p\n\n", p);

#if PROFILE
        loop_body_time[wid] += (rdtsc() - work_begin);
#endif
      } // while ( !done && !misspec )

      // Misspec happened in some stage. Just return here

      if (misspec)
      {
        free( try_commit_buffer );
        return;
      }
    } // for (i = 0 ; i < num_stages ; i++)

    // This iteration is good to go. Send pending writes to the commit stage

    DBG("ver_try_commit, forward to commit for iter %d\n", iter);

    forward_values_to_commit( wid, try_commit_buffer, buf_index );

    // If all the workers are finished, return.

    if ( terminated_iter != -1 )
    {
      free( try_commit_buffer );
      return;
    }

    advance_iter();
  } // while( TRUE )
}

void commit(uint32_t n_all_workers, pid_t* worker_pids)
{
  char* terminated = (char*)malloc(sizeof(char)*n_all_workers);
  memset(terminated, 0, sizeof(char)*n_all_workers);

  bool* wait = (bool*)calloc(sizeof(bool), num_aux_workers);
  bool* eoi = (bool*)calloc(sizeof(bool), num_aux_workers);

  unsigned stage = 0;
  unsigned num_stages = GET_NUM_STAGES();

  while ( true )
  {
    for (unsigned i = 0 ; i < num_aux_workers ; i++)
    {
      if ( wait[i] ) continue;

      queue_t* commit_queue = commit_queues[i];

      while ( !__sw_queue_empty( commit_queue ) )
      {
        packet* p = (packet*)__sw_queue_consume( commit_queue );

#if PROFILE
        incoming_bw[MAX_WORKERS] += sizeof(packet);
#endif

        DUMPPACKET("[commit], ", p);

        switch (p->type)
        {
          case NORMAL:
          {
            // packet comes as a pair
            packet* shadow_p = (packet*)__sw_queue_consume(commit_queue);
            update_normal_packet(p, shadow_p);

#if PROFILE
            incoming_bw[MAX_WORKERS] += sizeof(packet);
            m_incoming_bw[MAX_WORKERS] += (2 * sizeof(packet));
            e_incoming_bw[MAX_WORKERS] += (2 * sizeof(packet));
#endif

            // mark shadow packet read
            shadow_p->ptr = NULL;

            break;
          }
          case BOI:
            assert( false );
            break;
          case SUPER:
          {
            // super packet comes as a pair
            packet* shadow_p = (packet*)__sw_queue_consume(commit_queue);
            update_super_packet(0, p, shadow_p);

#if PROFILE
            incoming_bw[MAX_WORKERS] += sizeof(packet);
            incoming_bw[MAX_WORKERS] += sizeof(packet_chunk);
            incoming_bw[MAX_WORKERS] += sizeof(packet_chunk);

            unsigned m = 0, e = 0;

            packet_chunk* shadow_chunk = (packet_chunk*)(shadow_p->value);
            profile_packet((int8_t*)(shadow_chunk->data), PAGE_SIZE, m, e);

            m_incoming_bw[MAX_WORKERS] += (m + sizeof(packet)) * 2;
            e_incoming_bw[MAX_WORKERS] += (e + sizeof(packet)) * 2;
#endif

            break;
          }
          case EOI:
          {
            eoi[i] = true;

            bool synched = true;
            for (unsigned j = 0 ; j < num_aux_workers ; j++)
            {
              if ( eoi[j] ) continue;

              synched = false;
              break;
            }

            if (!synched)
            {
              wait[i] = true;
              break;
            }

            // all try-commits reach EOI

            for (unsigned j = 0 ; j < num_aux_workers ; j++)
            {
              eoi[j] = false;
              wait[j] = false;
            }

            if ( stage == num_stages-1 )
            {
              PCB*      pcb = get_pcb();
              Iteration iter = PREFIX(current_iter)();
              pcb->last_committed_iteration = iter;
              DBG("ver_commit, iter %d has been committed\n", iter);

              if ( PREFIX(current_iter)() == 0 )
              {
                update_loop_invariant_buffer();
                (*good_to_go) = 1;
              }

              advance_iter();
              stage = 0;
            }
            else
              stage++;
            break;
          }
          case EOW:
            break;
          case ALLOC:
          {
            DBG("commit unit handles ALLOC packet\n");

            Iteration iter = PREFIX(current_iter)();
            Wid wid_offset = (Wid)iter % GET_REPLICATION_FACTOR(stage);
            Wid source_wid = GET_FIRST_WID_OF_STAGE(stage) + wid_offset;

            if (p->size == 0)
            {
#if PROFILE
              incoming_bw[MAX_WORKERS] += sizeof(packet_chunk);
              m_incoming_bw[MAX_WORKERS] += sizeof(packet_chunk);
              e_incoming_bw[MAX_WORKERS] += sizeof(packet_chunk);
              alloc_incoming_bw[MAX_WORKERS] += sizeof(packet_chunk);
#endif
              unsigned valids = 0;
              packet_chunk*      chunk = (packet_chunk*)(p->value);
              VerMallocInstance* vmi = (VerMallocInstance*)(chunk->data);
              for (unsigned k = 0 ; k < PAGE_SIZE / sizeof(VerMallocInstance) ; k++)
              {
                if (!vmi->ptr) break;

                if (vmi->heap == -1)
                {
                  update_ver_malloc( source_wid, vmi->size, (void*)vmi->ptr );
                }
                else
                {
                  update_ver_separation_malloc( vmi->size, source_wid, (unsigned)(uint64_t)(vmi->heap), (void*)vmi->ptr );
                }

                if (iter == 0)
                {
                  // memalloc happened in iteration zero. EVERYONE should know this.

                  DBG("broadcast alloc from the commit process, %p\n", (void*)vmi->ptr);

                  // pack process id and heap id into value field
                  uint64_t value;
                  int16_t  write;
                  if (vmi->heap == -1)
                  {
                    value = (uint64_t)(source_wid);
                    write = REGULAR;
                  }
                  else
                  {
                    value = ((uint64_t)source_wid << 32) | ((uint64_t)(vmi->heap) & 0xffffffff);
                    write = SEPARATION;
                  }

                  commit_queue_broadcast_event(MAIN_PROCESS_WID, (void*)vmi->ptr, vmi->size, value, write, ALLOC);
                }

                valids++;
                vmi++;
              }
              mark_packet_chunk_read(0, chunk);

              break;
            }
            break;
          }
          case FREE:
          {
            Iteration iter = PREFIX(current_iter)();
            Wid wid_offset = (Wid)iter % GET_REPLICATION_FACTOR(stage);
            Wid source_wid = GET_FIRST_WID_OF_STAGE(stage) + wid_offset;

            update_ver_free( source_wid, p->ptr );
            break;
          }
          default:
            assert( false && "Unsupported type of packet in commit stage" );
        }

        // mark the packet consumed
        p->ptr = NULL;

        if (wait[i])
          break;
      }
    }

    // test if all workers have finished

    bool all_terminated = true;

    Wid  wid = 0;
    for ( ; wid < n_all_workers ; wid++)
    {
      int status;
      int ret = waitpid( worker_pids[wid], &status, WNOHANG );

      if ( (ret == 0) || !WIFEXITED(status) )
      {
        all_terminated = false;
        break;
      }
      else
      {
        if (terminated[wid] == 0)
          DBG("ver_commit, wid %u exited with status %u\n", wid, WEXITSTATUS(status) );
        terminated[wid] = 1;
      }
    }

    // check queue emptiness agian to prevent the case of a worker puts something in the queue and
    // terminated between the while loop and the for loop above

    bool empty = true;
    for (unsigned i = 0 ; i < num_aux_workers ; i++)
    {
      queue_t* commit_queue = commit_queues[i];
      if ( !__sw_queue_empty( commit_queue ) )
        empty = false;
    }

    if ( all_terminated && empty )
      break;
  }

  free(terminated);
}

}
