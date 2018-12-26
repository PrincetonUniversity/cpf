#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "internals/constants.h"
#include "internals/control.h"
#include "internals/debug.h"
#include "internals/profile.h"
#include "internals/strategy.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/packet.h"
#include "internals/smtx/smtx.h"

namespace specpriv_smtx
{

// Queues for uncommitted page forwarding 
// ucvp_queues[from_worker][to_worker] = queue_t*

queue_t*** ucvf_queues; 

// Queus for speculative read/write. Sent to try-commit unit

queue_t*** queues;

// A Queue for try-commit to commit

queue_t** commit_queues;

// A queue from commit to the workers. 

queue_t** reverse_commit_queues;

void init_queues(unsigned n_workers, unsigned n_aux_workers)
{
  unsigned i, j;
  int prot = PROT_WRITE | PROT_READ;
  int flags = MAP_SHARED | MAP_ANONYMOUS;

  ucvf_queues = (queue_t***)mmap(0, sizeof(queue_t**)*n_workers, prot, flags, -1, 0);

  for (i = 0 ; i < n_workers ; i++)
  {
    ucvf_queues[i] = (queue_t**)mmap(0, sizeof(queue_t*)*n_workers, prot, flags, -1, 0);

    for (j = i+1 ; j < n_workers ; j++)
      if ( GET_MY_STAGE(i) == GET_MY_STAGE(j) )
        continue;
      else
        ucvf_queues[i][j] = __sw_queue_create();
  }

  // queues for misspec detection and committing
  // - each non-aux worker try_commit proess
  // - (n_workers) number of queues

  queues = (queue_t***)mmap(0, sizeof(queue_t**)*n_workers, prot, flags, -1, 0);

  for (i = 0 ; i < n_workers ; i++)
  {
    queues[i] = (queue_t**)mmap(0, sizeof(queue_t*)*n_aux_workers, prot, flags, -1, 0);

    for (j = 0 ; j < n_aux_workers ; j++)
      queues[i][j] = __sw_queue_create();
  }

  // queue for the try-commit process to the commit process 

  commit_queues = (queue_t**)mmap(0, sizeof(queue_t*)*n_aux_workers, prot, flags, -1, 0);

  for (i = 0 ; i < n_aux_workers ; i++)
    commit_queues[i] = __sw_queue_create();
  
  // queue for the commit process to the worker process

  reverse_commit_queues = (queue_t**)mmap(0, sizeof(queue_t*)*n_workers, prot, flags, -1, 0);

  for (i = 0 ; i < n_workers ; i++)
    reverse_commit_queues[i] = __sw_queue_create();
}

void fini_queues(unsigned n_workers, unsigned n_aux_workers)
{
  // wrap up for ucvf_queue

  unsigned i, j;

  for (i = 0 ; i < n_workers ; i++)
  {
    for (j = 0 ; j < n_workers ; j++)
    {
      queue_t* queue = ucvf_queues[i][j];
      if (queue)
        __sw_queue_free(queue); 
    }
    munmap(ucvf_queues[i], sizeof(queue_t*)*n_workers);
  }
  munmap(ucvf_queues, sizeof(queue_t**)*n_workers);

  // wrap up for queue

  for (i = 0 ; i < n_workers ; i++)
  {
    for (j = 0 ; j < n_aux_workers ; j++)
    {
      queue_t* queue = queues[i][j];
      if (queue)
        __sw_queue_free(queue);
    }
    munmap(queues[i], sizeof(queue_t*)*n_aux_workers);
  }
  munmap(queues, sizeof(queue_t**)*n_workers);

  // wrap up for commit queue

  for (i = 0 ; i < n_aux_workers ; i++)
    __sw_queue_free(commit_queues[i]);
  munmap(commit_queues, sizeof(queue_t*)*n_aux_workers);

  // wrap up reverse commit queue
  for (i = 0 ; i < n_workers ; i++)
    __sw_queue_free(reverse_commit_queues[i]);
  munmap(reverse_commit_queues, sizeof(queue_t*)*n_workers);
}

static packet_chunk* create_packet_chunk(Wid wid, int8_t* addr)
{
#if PROFILE
  //uint64_t b = rdtsc();
#endif
  packet_chunk* ret = get_available_packet_chunk(wid);
#if PROFILE
  //packet_chunk_wait_time[ wid ] += ( rdtsc()-b );
#endif

  // create signature: 
  //
  // -- if wid >= try_commit_begin
  //   set sign[0] to 1, and clear sign[0] when commit unit reads it.
  //
  // -- else
  //   if the packet should be read by stage n, set sign[n] to 1.
  //   sign[0] is always set to 1, and cleared when commit unit consumes the packet

  (ret->sign)[0] = 1;
  if ( wid < try_commit_begin )
  {
    uint64_t i = GET_MY_STAGE(wid)+1;
    uint64_t e = GET_NUM_STAGES();
    for ( ; i < e ; i++)
      (ret->sign)[i] = 1;
  }

  memcpy( ret->data, addr, PAGE_SIZE );

  return ret;
}

packet* create_packet(Wid wid, void* ptr, void* value, uint32_t size, int16_t is_write, int16_t type)
{
  packet* p;

  if (wid == MAIN_PROCESS_WID)
    p = get_available_commit_process_packet();
  else
    p = get_available_packet(wid);

  p->ptr = ptr;
  p->value = value;
  p->size = size;
  p->is_write = is_write;
  p->type = type;

  return p;
}

/*
 * "forward" sends information only to workers that handles following stages of the same iteration 
 * "broadcast" sends information to all the worker for following stages
 */

unsigned forward_page(Wid wid, Iteration iter, void* addr, int16_t check)
{
  unsigned count = 0;
  unsigned my_stage = GET_MY_STAGE(wid);

  // create a packet chunk for the page

  packet_chunk* chunk = create_packet_chunk(wid, (int8_t*)addr);

  for (unsigned i=my_stage+1, e=GET_NUM_STAGES() ; i<e ; i++)
  {
    // if there are multiple workers for a stage 'i', which worker will run the iteration 'iter'?

    Wid wid_offset = (Wid)iter % GET_REPLICATION_FACTOR(i);

    // find wid of the worker that runs the 'iter' iterations of stage 'i'

    Wid target_wid = GET_FIRST_WID_OF_STAGE(i) + wid_offset;

    // create a packet that holds packet chunk as a data

    packet* p = create_packet(wid, addr, (void*)chunk, sizeof(packet_chunk), check, SUPER);
    DUMPPACKET("[forwad_page] ", p);

    __sw_queue_produce( ucvf_queues[wid][target_wid], (void*)p );

#if PROFILE
    outgoing_bw[wid] += sizeof(packet);
    num_sent_packets[wid] += 1; 
    count += 1;
#endif
  }

  // send packet to the try-commit
 
  packet* p = create_packet(wid, addr, (void*)chunk, sizeof(packet_chunk), check, SUPER);

  Wid target_try_commit_id = ((size_t)addr >> PAGE_SHIFT) % num_aux_workers;
  __sw_queue_produce( queues[wid][target_try_commit_id], (void*)p );

#if PROFILE
  outgoing_bw[wid] += (sizeof(packet) + sizeof(packet_chunk));
  num_sent_packets[wid] += 1; 
  num_sent_spackets[wid] += 1; 
  count += 1;
#endif

  return count;
}

unsigned forward_packet(Wid wid, Iteration iter, void* addr, void* value, uint32_t size, int16_t check)
{
  unsigned count = 0;

  assert( check != CHECK_FREE );
  unsigned my_stage = GET_MY_STAGE(wid);

  for (unsigned i=my_stage+1, e=GET_NUM_STAGES() ; i<e ; i++)
  {
    // if there are multiple workers for a stage 'i', which worker will run the iteration 'iter'?

    Wid wid_offset = (Wid)iter % GET_REPLICATION_FACTOR(i);

    // find wid of the worker that runs the 'iter' iterations of stage 'i'

    Wid target_wid = GET_FIRST_WID_OF_STAGE(i) + wid_offset;

    // create a packet that holds packet chunk as a data

    packet* p = create_packet(wid, addr, value, size, check, NORMAL);
    DUMPPACKET("[forwad_packet] ", p);

    __sw_queue_produce( ucvf_queues[wid][target_wid], (void*)p );

#if PROFILE
    outgoing_bw[wid] += sizeof(packet);
    num_sent_packets[wid] += 1; 
    count += 1;
#endif
  }

  // send packet to the try-commit

  packet* p = create_packet(wid, addr, value, size, check, NORMAL);

  Wid target_try_commit_id = ((size_t)addr >> PAGE_SHIFT) % num_aux_workers;
  __sw_queue_produce( queues[wid][target_try_commit_id], (void*)p );

#if PROFILE
  outgoing_bw[wid] += sizeof(packet);
  num_sent_packets[wid] += 1; 
  count += 1;
#endif

  return count;
}

void broadcast_malloc_chunk(Wid wid, int8_t* addr, uint32_t size)
{
  if ( wid >= try_commit_begin ) // do nothing for try-commit unit
    return;

  unsigned my_stage = GET_MY_STAGE(wid);
  unsigned i, j;

  unsigned num_chunks = (size / PAGE_SIZE) + ((size % PAGE_SIZE) ? 1 : 0);

  for (unsigned c = 0 ; c < num_chunks ; c++)
  {
    packet_chunk* chunk = create_packet_chunk(wid, addr);

    // send chunk to all following stages

    for (i = my_stage+1 ; i < GET_NUM_STAGES() ; i++)
    {
      // find the id of the worker that handles iteration 'iter' of stage 'i'

      j = GET_FIRST_WID_OF_STAGE(i);
      unsigned j_bound = j + GET_REPLICATION_FACTOR(i);

      for ( ; j < j_bound; j++ )
      {
        Wid target_wid = j;

        // send packet

        packet* p = create_packet(wid, (void*)NULL, (void*)chunk, 0, 0, ALLOC);

        __sw_queue_produce( ucvf_queues[wid][target_wid], (void*)p );

#if PROFILE
        outgoing_bw[wid] += sizeof(packet);
        m_outgoing_bw[wid] += sizeof(packet);
        e_outgoing_bw[wid] += sizeof(packet);
        alloc_outgoing_bw[wid] += sizeof(packet);
        num_sent_packets[wid] += 1; 
        ver_malloc_overhead[wid] += sizeof(packet);
#endif
      }
    }

    // send packet to the try-commit

    for (i = 0 ; i < num_aux_workers ; i++)
    {
      packet* p = create_packet(wid, (void*)NULL, (void*)chunk, 0, 0, ALLOC);
      __sw_queue_produce( queues[wid][i], (void*)p );

#if PROFILE
      outgoing_bw[wid] += sizeof(packet);
      m_outgoing_bw[wid] += sizeof(packet);
      e_outgoing_bw[wid] += sizeof(packet);
      alloc_outgoing_bw[wid] += sizeof(packet);
      num_sent_packets[wid] += 1; 
      ver_malloc_overhead[wid] += sizeof(packet);
#endif
    }

#if PROFILE
    outgoing_bw[wid] += sizeof(packet_chunk);
    m_outgoing_bw[wid] += sizeof(packet_chunk);
    e_outgoing_bw[wid] += sizeof(packet_chunk);
    alloc_outgoing_bw[wid] += sizeof(packet_chunk);
    num_sent_spackets[wid] += 1; 
    ver_malloc_overhead[wid] += sizeof(packet_chunk);
#endif

    addr += PAGE_SIZE;
  }
}

unsigned broadcast_event(Wid wid, void* ptr, uint32_t size, uint64_t value, int16_t write, int16_t type)
{
  unsigned count = 0;

  if ( wid >= try_commit_begin ) // do nothing for try-commit unit
    return 0;

  unsigned my_stage = GET_MY_STAGE(wid);
  unsigned i, j;

  // send notification to all following stages

  for (i = my_stage+1 ; i < GET_NUM_STAGES() ; i++)
  {
    // find the id of the worker that handles iteration 'iter' of stage 'i'

    j = GET_FIRST_WID_OF_STAGE(i);
    unsigned j_bound = j + GET_REPLICATION_FACTOR(i);

    for ( ; j < j_bound; j++ )
    {
      Wid target_wid = j;

      // send packet

      packet* p = create_packet(wid, (void*)ptr, (void*)value, size, write, type);

      __sw_queue_produce( ucvf_queues[wid][target_wid], (void*)p );

#if PROFILE
      outgoing_bw[wid] += sizeof(packet);
      num_sent_packets[wid] += 1; 
      count += 1;
#endif
    }
  }

  // send packet to the try-commit

  for (i = 0 ; i < num_aux_workers ; i++)
  {
    packet* p = create_packet(wid, (void*)ptr, (void*)value, size, write, type);

    __sw_queue_produce( queues[wid][i], (void*)p );

#if PROFILE
    outgoing_bw[wid] += sizeof(packet);
    num_sent_packets[wid] += 1; 
    count += 1;
#endif
  }

  return count;
}

unsigned to_try_commit(Wid wid, void* ptr, uint32_t size, uint64_t value, int16_t write, int16_t type)
{
  unsigned count = 0;
  // send packet to the try-commit

  for (unsigned i = 0 ; i < num_aux_workers ; i++)
  {
    packet* p = create_packet(wid, (void*)ptr, (void*)value, size, write, type);
    __sw_queue_produce( queues[wid][i], (void*)p );

#if PROFILE
    outgoing_bw[wid] += sizeof(packet);
    num_sent_packets[wid] += 1; 
    count += 1;
#endif
  }

  return count;
}

void commit_queue_broadcast_event(Wid wid, void* ptr, uint32_t size, uint64_t value, int16_t write, int16_t type)
{
  for (unsigned i = 0 ; i < try_commit_begin ; i++)
  {
    packet* p = create_packet(wid, ptr, (void*)value, size, write, type);

    DBG("reverse_commit_queue, send packet to %u with queue %p\n", i, &reverse_commit_queues[i]);
    __sw_queue_produce( reverse_commit_queues[i], (void*)p );
  }
}

void clear_incoming_queues(Wid wid)
{
  unsigned i;

  DBG("clear_incoming_queue\n");

  if ( wid < try_commit_begin )
  {
    for (i = 0 ; i < wid ; i++)
    {
      if ( ucvf_queues[i][wid] )
        __sw_queue_clear( ucvf_queues[i][wid] );
      DBG("clear_incoming_queue, ucvf, %d->%d done\n", i, wid);
    }
  }
  else
  {
    for (i = 0 ; i < try_commit_begin ; i++)
    {
      __sw_queue_clear( queues[i][wid-try_commit_begin] );
      DBG("clear_incoming_queue, try_commit, from %d done\n", i);
    }
  }
}

}
