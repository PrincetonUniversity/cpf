#ifndef LLVM_LIBERTY_SPEC_PRIV_SMTX_PACKET_H
#define LLVM_LIBERTY_SPEC_PRIV_SMTX_PACKET_H

#include "internals/constants.h"
#include "internals/types.h"

namespace specpriv_smtx
{

struct packet
{
  void*    ptr;
  void*    value;
  uint32_t size;
  int16_t  is_write;
  int16_t  type;
  uint64_t sign;
};

extern const char* packettypestr[8];

struct packet_chunk
{
  uint8_t sign[MAX_STAGE_NUM];  // assert (MAX_STAGE_NUM <= 64)
  int8_t  data[PAGE_SIZE];
};

void init_packets(unsigned n_all_workers);
void fini_packets(unsigned n_all_workers);

packet_chunk* get_available_packet_chunk(Wid wid);
void          mark_packet_chunk_read(unsigned stage, packet_chunk* chunk);

packet* get_available_packet(uint32_t wid);
packet* get_available_commit_process_packet();

void DUMPPACKET(const char* str, packet* p);

}

#endif
