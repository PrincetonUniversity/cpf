#include <stdint.h>

#include <set>

namespace specpriv_smtx
{

struct SeparationHeap
{
  uint64_t begin;
  uint64_t end;
  uint64_t next;
  uint64_t nextchunk;
};

void separation_init(unsigned num_non_versioned_heaps, unsigned num_versioned_heaps);
void separation_fini(unsigned num_non_versioned_heaps, unsigned num_versioned_heaps);

void register_ro(unsigned heap);
void register_nrbw(unsigned heap);
void register_stage_private(unsigned heap, unsigned stage);

bool is_in_uc(void* page);
bool is_in_ro(void* page);
bool is_in_nrbw(void* page);
int  is_in_stage_private(void* page);

void set_separation_heaps_prot_none();

std::set<unsigned>* get_uc();
std::set<unsigned>* get_versioned_uc();
std::set<unsigned>* get_nrbw();
std::set<unsigned>* get_versioned_nrbw();
std::set<unsigned>* get_stage_private(unsigned stage);
std::set<unsigned>* get_versioned_stage_private(unsigned stage);

uint64_t heap_begin(unsigned heap);
uint64_t heap_bound(unsigned heap);
uint64_t versioned_heap_begin(unsigned wid, unsigned heap);
uint64_t versioned_heap_bound(unsigned wid, unsigned heap);

void* update_ver_separation_malloc(size_t size, unsigned wid, unsigned heap, void* ptr);

}
