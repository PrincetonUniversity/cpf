#include "internals/profile.h"
#include "internals/debug.h"

#include <stdio.h>
#include <stdlib.h>

#if (PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)

namespace specpriv_smtx
{

// data structures for performance debugging

uint64_t* volatile execution_time;
uint64_t* volatile loop_body_time;
uint64_t* volatile loop_body_time_buf;
uint64_t* volatile ucvf_stall_time;
uint64_t* volatile verification_stall_time;
uint64_t* volatile packet_chunk_wait_time;
uint64_t* volatile begin_iter_time;
uint64_t* volatile end_iter_time;
uint64_t* volatile forward_time;
uint64_t* volatile memset_time;
uint64_t* volatile globals_time;
uint64_t* volatile begin_iter_dominator;
uint64_t* volatile end_iter_dominator;
uint64_t* volatile r1;
uint64_t* volatile r2;
uint64_t* volatile r4;
uint64_t* volatile r8;
uint64_t* volatile rmem;
uint64_t* volatile w1;
uint64_t* volatile w2;
uint64_t* volatile w4;
uint64_t* volatile w8;
uint64_t* volatile wmem;
uint64_t* volatile first_iter_overhead;
uint64_t* volatile ver_malloc_overhead;
uint64_t* volatile ver_malloc_max;

uint64_t* volatile process_incoming_packet_time;
uint64_t* volatile reset_page_protection_time;
uint64_t* volatile incoming_pages;
uint64_t* volatile reset_pages;
uint64_t* volatile send_outgoing_heap_time;
uint64_t* volatile outgoing_heaps;
uint64_t* volatile heap_pages;

uint64_t* volatile incoming_bw;
uint64_t* volatile outgoing_bw;
uint64_t* volatile m_incoming_bw; // m for meaningful
uint64_t* volatile m_outgoing_bw;
uint64_t* volatile e_incoming_bw; // e for essential
uint64_t* volatile e_outgoing_bw;
uint64_t* volatile ro_incoming_bw;
uint64_t* volatile ro_outgoing_bw;
uint64_t* volatile alloc_incoming_bw;
uint64_t* volatile alloc_outgoing_bw;
uint64_t* volatile read_only_pages;
uint64_t* volatile read_only_chunks;
uint64_t* volatile num_sent_packets;
uint64_t* volatile num_sent_spackets;
uint64_t* volatile m_total;
uint64_t* volatile e_total;
uint64_t* volatile p_total;

uint64_t  volatile r1count;
uint64_t  volatile r2count;
uint64_t  volatile r4count;
uint64_t  volatile r8count;
uint64_t  volatile rmemcount;
uint64_t  volatile w1count;
uint64_t  volatile w2count;
uint64_t  volatile w4count;
uint64_t  volatile w8count;
uint64_t  volatile wmemcount;

void init_profile()
{
  execution_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  loop_body_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  loop_body_time_buf = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  ucvf_stall_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  verification_stall_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  packet_chunk_wait_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  begin_iter_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  end_iter_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  forward_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  memset_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  globals_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  begin_iter_dominator = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  end_iter_dominator = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  r1 = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  r2 = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  r4 = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  r8 = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  rmem = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  w1 = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  w2 = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  w4 = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  w8 = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  wmem = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  first_iter_overhead = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  ver_malloc_overhead = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  ver_malloc_max = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );

  process_incoming_packet_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  reset_page_protection_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  incoming_pages = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  reset_pages = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  send_outgoing_heap_time = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  outgoing_heaps = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  heap_pages = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );

  incoming_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  outgoing_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  m_incoming_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  m_outgoing_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  e_incoming_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  e_outgoing_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  ro_incoming_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  ro_outgoing_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  alloc_incoming_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  alloc_outgoing_bw = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  read_only_pages = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  read_only_chunks = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  num_sent_packets = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  num_sent_spackets = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  m_total = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  e_total = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );
  p_total = (uint64_t*)calloc( sizeof(uint64_t), (unsigned)(MAX_WORKERS+1) );

  r1count = 0;
  r2count = 0;
  r4count = 0;
  r8count = 0;
  rmemcount = 0;
  w1count = 0;
  w2count = 0;
  w4count = 0;
  w8count = 0;
  wmemcount = 0;
}

void dump_profile(Wid wid)
{
  uint64_t exec = rdtsc()-execution_time[wid];
  uint64_t lb = loop_body_time[wid];
  uint64_t us = ucvf_stall_time[wid];
  uint64_t vs = verification_stall_time[wid];
  uint64_t pw = packet_chunk_wait_time[wid];
  uint64_t bs = begin_iter_time[wid];
  uint64_t es = end_iter_time[wid];
  uint64_t fs = forward_time[wid];
  uint64_t ms = (rdtsc()-memset_time[wid]);
  uint64_t gs = globals_time[wid];
  uint64_t bid = begin_iter_dominator[wid];
  uint64_t eid = end_iter_dominator[wid];
  uint64_t r1s = r1[wid];
  uint64_t r2s = r2[wid];
  uint64_t r4s = r4[wid];
  uint64_t r8s = r8[wid];
  uint64_t rmems = rmem[wid];
  uint64_t w1s = w1[wid];
  uint64_t w2s = w2[wid];
  uint64_t w4s = w4[wid];
  uint64_t w8s = w8[wid];
  uint64_t wmems = wmem[wid];
  uint64_t ib = incoming_bw[wid];
  uint64_t ob = outgoing_bw[wid];
  uint64_t mib = m_incoming_bw[wid];
  uint64_t mob = m_outgoing_bw[wid];
  uint64_t eib = e_incoming_bw[wid];
  uint64_t eob = e_outgoing_bw[wid];
  uint64_t roib = ro_incoming_bw[wid];
  uint64_t roob = ro_outgoing_bw[wid];
  uint64_t aib = alloc_incoming_bw[wid];
  uint64_t aob = alloc_outgoing_bw[wid];
  uint64_t vm = ver_malloc_overhead[wid];

  PROFDUMP("\n---- profile ----\n"
      "wid: %u total %lu, loop_body : %lu(%.2f%%)\n"
      "begin_iter: %lu(%.2f%%), end_iter: %lu(%.2f%%)\n"
      "forward: %lu(%.2f%%)\n" 
      "worker-wrapup: %lu(%.2f%%)\n" 
      "process_incoming_packet: %lu(%.2f%%)\n"
      "begin_iter_dominator: %lu(%.2f%%)\n"
      "end_iter_dominator: %lu(%.2f%%)\n" 
      "ucvf_stall: %lu(%.2f%%), verification_stall: %lu(%.2f%%), packet_chunk_wait: %lu(%.2f%%)\n"
      "ver_malloc: %lu(%.2f%%), max: %lu\n"
      "process_incoming_packet_time: %lu\n"
      "process_incoming_pages: %lu\n"
      "reset_pages_time: %lu\n"
      "reset_pages: %lu\n"
      "send_outgoing_heap_time: %lu\n"
      "outgoing_heap_pages: %lu\n"
      "heap_pages: %lu\n"
      "first_iter_overhead: %lu\n"
      "r1: %lu, r2: %lu, r4: %lu, r8: %lu, rtotal: %lu\n"
      "w1: %lu, w2: %lu, w4: %lu, w8: %lu, wtotal: %lu\n"
      "total_incoming_bw: %lu, total_outgoing_bw: %lu\n"
      "meaningful_incoming_bw: %lu, meaningful_outgoing_bw: %lu\n"
      "essential_incoming_bw: %lu, essential_outgoing_bw: %lu\n"
      "readonly_incoming_bw: %lu, readonly_outgoing_bw: %lu\n"
      "alloc_incoming_bw: %lu, alloc_outgoing_bw: %lu\n"
      "packets: %lu, super_packets: %lu\n"
      "m_total: %lu(%.2f%%), e_total: %lu(%.2f%%), p_total: %lu\n"
      "read_only_pages: %lu, read_only_chunks: %lu\n\n", 
      wid, exec, lb, (float)lb/(double)exec*100, 
      bs, (float)bs/(double)exec*100, es, (float)es/(double)exec*100, 
      fs, (float)fs/(double)exec*100, 
      ms, (float)ms/(double)exec*100, 
      gs, (float)gs/(double)exec*100, 
      bid, (float)bid/(double)exec*100, 
      eid, (float)eid/(double)exec*100, 
      us, (float)us/(double)exec*100, vs, (float)vs/(double)exec*100, pw, (float)pw/(double)exec*100,
      vm, (float)vm/(double)exec*100, ver_malloc_max[wid],
      process_incoming_packet_time[wid],
      incoming_pages[wid],
      reset_page_protection_time[wid],
      reset_pages[wid],
      send_outgoing_heap_time[wid],
      outgoing_heaps[wid],
      heap_pages[wid],
      first_iter_overhead[wid],
      r1s, r2s, r4s, r8s, r1s+r2s+r4s+r8s,
      w1s, w2s, w4s, w8s, w1s+w2s+w4s+w8s,
      ib, ob,
      mib, mob,
      eib, eob,
      roib, roob,
      aib, aob,
      num_sent_packets[wid], num_sent_spackets[wid],
      m_total[wid], m_total[wid]/(double)p_total[wid]*100, e_total[wid], e_total[wid]/(double)p_total[wid]*100, p_total[wid],
      read_only_pages[wid], read_only_chunks[wid]
      );
}

void fini_profile()
{
  free(execution_time);
  free(loop_body_time);
  free(loop_body_time_buf);
  free(ucvf_stall_time);
  free(verification_stall_time);
  free(begin_iter_time);
  free(end_iter_time);
  free(forward_time);
  free(memset_time);
  free(globals_time);
  free(begin_iter_dominator);
  free(end_iter_dominator);
  free(r1);
  free(r2);
  free(r4);
  free(r8);
  free(rmem);
  free(w1);
  free(w2);
  free(w4);
  free(w8);
  free(wmem);
  free(incoming_bw);
  free(outgoing_bw);
  free(m_incoming_bw);
  free(m_outgoing_bw);
  free(e_incoming_bw);
  free(e_outgoing_bw);
  free(ro_incoming_bw);
  free(ro_outgoing_bw);
  free(alloc_incoming_bw);
  free(alloc_outgoing_bw);
  free(read_only_pages);
  free(read_only_chunks);
  free(num_sent_packets);
  free(num_sent_spackets);
  free(m_total);
  free(e_total);
  free(p_total);

  free(process_incoming_packet_time);
  free(reset_page_protection_time);
  free(incoming_pages);
  free(reset_pages);
  free(send_outgoing_heap_time);
  free(outgoing_heaps);
  free(heap_pages);
}

// data structures for performance debugging

uint64_t rdtsc(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
}

}
#endif
