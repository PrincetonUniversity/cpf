#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "internals/constants.h"
#include "internals/debug.h"
#include "internals/smtx/packet.h"

namespace specpriv_smtx
{

const char* packettypestr[8] = { "NORMAL", "PAGE", "Beginnin-Of-Iteration", "End-Of-Iteration", "MISSPEC", "End-Of-Worker", "ALLOC", "FREE" };

// per-worker packet pools

static packet** packet_begins;
static packet** packet_ends;
static packet** packet_nexts;

// per-worker buffer for super packets

static packet_chunk** packet_chunk_begins;
static packet_chunk** packet_chunk_ends;
static packet_chunk** packet_chunk_nexts;

// commit-process packet pool

static packet* commit_process_packet_begin;
static packet* commit_process_packet_end;
static packet* commit_process_packet_next;

/*
 * initialize
 */

void init_packets(unsigned n_all_workers)
{
  // create a packet pool for each worker (including aux workers)

  int prot = PROT_WRITE | PROT_READ;
  int flags = MAP_SHARED | MAP_ANONYMOUS;

  packet_begins = (packet**)mmap(0, sizeof(packet*)*n_all_workers, prot, flags, -1, 0);
  packet_ends = (packet**)mmap(0, sizeof(packet*)*n_all_workers, prot, flags, -1, 0);
  packet_nexts = (packet**)mmap(0, sizeof(packet*)*n_all_workers, prot, flags, -1, 0);

  // data structure to managing the packet pool

  unsigned i;
  for (i = 0 ; i < n_all_workers ; i++)
  {
    size_t size = sizeof(packet)*PACKET_POOL_SIZE;
    packet_begins[i] = (packet*)mmap(0, size, prot, flags, -1, 0);
    packet_nexts[i] = packet_begins[i];
    packet_ends[i]  = (packet*)( (size_t)packet_begins[i] + size );

    DBG("packet pool for %d, begins: %p, ends: %p, size %lu\n", i, packet_begins[i], packet_ends[i], size);
  }

  commit_process_packet_begin = (packet*)mmap(0, sizeof(packet)*PACKET_POOL_SIZE, prot, flags, -1, 0);
  commit_process_packet_next = commit_process_packet_begin;
  commit_process_packet_end = (packet*)( (size_t)commit_process_packet_begin + sizeof(packet)*PACKET_POOL_SIZE );

  // allocate space for packet_chunks

  packet_chunk_begins = (packet_chunk**)mmap(0, sizeof(packet_chunk*)*n_all_workers, prot, flags, -1, 0);
  packet_chunk_ends = (packet_chunk**)mmap(0, sizeof(packet_chunk*)*n_all_workers, prot, flags, -1, 0);
  packet_chunk_nexts = (packet_chunk**)mmap(0, sizeof(packet_chunk*)*n_all_workers, prot, flags, -1, 0);

  for (i = 0 ; i < n_all_workers ; i++)
  {
    size_t size = sizeof(packet_chunk)*PACKET_POOL_SIZE;
    packet_chunk_begins[i] = (packet_chunk*)mmap(0, size, prot, flags, -1, 0);
    packet_chunk_nexts[i] = packet_chunk_begins[i];
    packet_chunk_ends[i]  = (packet_chunk*)( (size_t)packet_chunk_begins[i] + size );

    DBG("packet pool for %d, begins: %p, ends: %p, size %lu\n", i, packet_chunk_begins[i], packet_chunk_ends[i], size);
  }
}

void fini_packets(unsigned n_all_workers)
{
  unsigned i;
  for (i = 0 ; i < n_all_workers ; i++)
  {
    munmap(packet_begins[i], sizeof(packet)*PACKET_POOL_SIZE);
    munmap(packet_chunk_begins[i], sizeof(packet)*PACKET_POOL_SIZE);
  }

  munmap(packet_begins, sizeof(packet*)*n_all_workers);
  munmap(packet_ends, sizeof(packet*)*n_all_workers);
  munmap(packet_nexts, sizeof(packet*)*n_all_workers);
  munmap(packet_chunk_begins, sizeof(packet*)*n_all_workers);
  munmap(packet_chunk_ends, sizeof(packet*)*n_all_workers);
  munmap(packet_chunk_nexts, sizeof(packet*)*n_all_workers);
}

/*
 * packet chunk management functions
 */

static bool is_chunk_available(packet_chunk* chunk)
{
  uint64_t* ptr = (uint64_t*)chunk->sign;
  if ( !ptr[0] && !ptr[1] && !ptr[2] && !ptr[3] && !ptr[4] && !ptr[5] && !ptr[6] && !ptr[7] )
    return true;
  else
    return false;
}

packet_chunk* get_available_packet_chunk(Wid wid)
{
  packet_chunk* bound = packet_chunk_ends[wid];
  packet_chunk* next = packet_chunk_nexts[wid];

  while ( !is_chunk_available(next) )
  {
    next++;
    if ( next >= bound )
      next = packet_chunk_begins[wid];
  }

  packet_chunk_nexts[wid] = (next+1) >= bound ? packet_chunk_begins[wid] : (next+1);

  return next;
}

void mark_packet_chunk_read(unsigned stage, packet_chunk* chunk)
{
  (chunk->sign)[stage] = 0;
}

/*
 * packet management functions
 */

static inline bool is_packet_available(packet* p)
{
  return (p->ptr == NULL);
}

packet* get_available_packet(uint32_t wid)
{
  packet* bound = packet_ends[wid];
  packet* next = packet_nexts[wid];

  while ( !is_packet_available(next) )
  {
    next++; 
    if (next >= bound)
      next = packet_begins[wid];
  }

  packet_nexts[wid] = (next+1) >= bound ? packet_begins[wid] : (next+1);

  return next;
}

packet* get_available_commit_process_packet()
{
  packet* bound = commit_process_packet_end;
  packet* next = commit_process_packet_next;

  while ( !is_packet_available(next) )
  {
    next++; 
    if (next >= bound)
      next = commit_process_packet_begin;
  }

  commit_process_packet_next = 
    (next+1) >= bound ? commit_process_packet_begin : (next+1);

  return next;
}


/*
 * debug
 */

void DUMPPACKET(const char* str, packet* p)
{
  DBG( "%spacket: %p, ptr: %p, value* %p, size: %u, write: %d, type: %s\n", 
    str, p, p->ptr, p->value, p->size, p->is_write, packettypestr[ p->type ]);
}

}
