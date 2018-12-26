#include <stdlib.h>

#include "internals/constants.h"

namespace specpriv_smtx
{

struct VerMallocChunk
{
  uint64_t begin;
  uint64_t end;
  uint64_t next;
  uint64_t nextchunk;
};

struct VerMallocInstance
{
  uint64_t ptr;
  uint32_t size;
  int32_t  heap;
};

struct VerMallocBuffer
{
  VerMallocInstance elem[PAGE_SIZE];
  uint64_t          index;
};

extern VerMallocChunk*  ver_malloc_chunks;
extern VerMallocBuffer* ver_malloc_buffer;

void  ver_malloc_init();
void  ver_malloc_fini();
void* ver_malloc(unsigned stage, size_t size);
void* update_ver_malloc(unsigned stage, size_t size, void* ptr);
void  ver_free(unsigned stage, void* ptr);
void  update_ver_free(unsigned stage, void* ptr);
void  buffer_ver_malloc(unsigned wid, uint64_t ptr, uint32_t size, int32_t heap);

}
