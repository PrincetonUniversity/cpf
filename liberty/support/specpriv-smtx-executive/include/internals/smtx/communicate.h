#include "internals/smtx/packet.h"
#include "internals/sw_queue/sw_queue.h"

namespace specpriv_smtx
{

extern queue_t*** ucvf_queues; 
extern queue_t*** queues;
extern queue_t**  commit_queues;
extern queue_t**  reverse_commit_queues;

void init_queues(unsigned n_workers, unsigned n_aux_workers);
void fini_queues(unsigned n_workers, unsigned n_aux_workers);

packet* create_packet(Wid wid, void* ptr, void* value, uint32_t size, int16_t is_write, int16_t
    type);

unsigned forward_page(Wid wid, Iteration iter, void* addr, int16_t check);
unsigned forward_packet(Wid wid, Iteration iter, void* addr, void* value, uint32_t size, int16_t check);

unsigned broadcast_event(Wid wid, void* ptr, uint32_t size, uint64_t value, int16_t write, int16_t type);
void     broadcast_malloc_chunk(Wid wid, int8_t* ptr, uint32_t size);

unsigned to_try_commit(Wid wid, void* ptr, uint32_t size, uint64_t value, int16_t write, int16_t type);
void     commit_queue_broadcast_event(Wid wid, void* ptr, uint32_t size, uint64_t value, int16_t write, int16_t type);

void clear_incoming_queues(Wid wid);

}
