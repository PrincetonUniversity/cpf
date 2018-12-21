#include <set>
#include <vector>
#include <tr1/unordered_map>

#include <stdint.h>

namespace specpriv_smtx
{

extern char* stack_begin;
extern char* stack_bound;

extern std::vector<uint8_t*>* shadow_globals; 
extern std::vector<uint8_t*>* shadow_stacks; 
extern std::set<uint8_t*>* shadow_heaps; 

extern std::tr1::unordered_map<void*, size_t>* heap_size_map;

extern Wid try_commit_begin;

void set_try_commit_begin(Wid wid);
void reset_try_commit_begin();

}
