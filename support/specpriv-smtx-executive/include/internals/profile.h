#include "internals/constants.h"

#if (PROFILE || PROFILE_MEMOPS || PROFILE_WEIGHT)
namespace specpriv_smtx
{

extern uint64_t* volatile execution_time;
extern uint64_t* volatile loop_body_time;
extern uint64_t* volatile loop_body_time_buf;
extern uint64_t* volatile ucvf_stall_time;
extern uint64_t* volatile verification_stall_time;
extern uint64_t* volatile packet_chunk_wait_time;
extern uint64_t* volatile begin_iter_time;
extern uint64_t* volatile end_iter_time;
extern uint64_t* volatile forward_time;
extern uint64_t* volatile memset_time;
extern uint64_t* volatile globals_time;
extern uint64_t* volatile begin_iter_dominator;
extern uint64_t* volatile end_iter_dominator;
extern uint64_t* volatile r1;
extern uint64_t* volatile r2;
extern uint64_t* volatile r4;
extern uint64_t* volatile r8;
extern uint64_t* volatile rmem;
extern uint64_t* volatile w1;
extern uint64_t* volatile w2;
extern uint64_t* volatile w4;
extern uint64_t* volatile w8;
extern uint64_t* volatile wmem;
extern uint64_t* volatile first_iter_overhead;
extern uint64_t* volatile ver_malloc_overhead;
extern uint64_t* volatile ver_malloc_max;
extern uint64_t* volatile read_only_pages;

extern uint64_t* volatile process_incoming_packet_time;
extern uint64_t* volatile reset_page_protection_time;
extern uint64_t* volatile incoming_pages;
extern uint64_t* volatile reset_pages;
extern uint64_t* volatile send_outgoing_heap_time;
extern uint64_t* volatile outgoing_heaps;
extern uint64_t* volatile heap_pages;

extern uint64_t* volatile incoming_bw;
extern uint64_t* volatile outgoing_bw;
extern uint64_t* volatile m_incoming_bw; // m for meaningful
extern uint64_t* volatile m_outgoing_bw;
extern uint64_t* volatile e_incoming_bw; // e for essential
extern uint64_t* volatile e_outgoing_bw;
extern uint64_t* volatile ro_incoming_bw;
extern uint64_t* volatile ro_outgoing_bw;
extern uint64_t* volatile alloc_incoming_bw;
extern uint64_t* volatile alloc_outgoing_bw;
extern uint64_t* volatile read_only_pages;
extern uint64_t* volatile read_only_chunks;
extern uint64_t* volatile num_sent_packets;
extern uint64_t* volatile num_sent_spackets;
extern uint64_t* volatile m_total;
extern uint64_t* volatile e_total;
extern uint64_t* volatile p_total;

extern uint64_t  volatile r1count;
extern uint64_t  volatile r2count;
extern uint64_t  volatile r4count;
extern uint64_t  volatile r8count;
extern uint64_t  volatile rmemcount;
extern uint64_t  volatile w1count;
extern uint64_t  volatile w2count;
extern uint64_t  volatile w4count;
extern uint64_t  volatile w8count;
extern uint64_t  volatile wmemcount;

void init_profile();
void dump_profile(Wid wid);
void fini_profile();
uint64_t rdtsc();

}
#endif

