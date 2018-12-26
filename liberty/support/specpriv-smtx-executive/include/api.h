#include <stdlib.h>

#include "internals/specpriv_queue.h"
#include "internals/types.h"

namespace specpriv_smtx
{

extern "C"
{
// control.cpp
int32_t PREFIX(num_available_workers)(void);
Wid     PREFIX(spawn_workers_callback)(int32_t current_iter, void (*callback)(int8_t*), int8_t* user);
Exit    PREFIX(join_children)(void);

// loopevent.cpp
unsigned PREFIX(begin_invocation)(void);
Exit     PREFIX(end_invocation)(void);
void     PREFIX(begin_iter)(void);
void     PREFIX(end_iter)(void);

// private.cpp
void      PREFIX(worker_finishes)(Exit exittaken);
Wid       PREFIX(my_worker_id)();
void      PREFIX(set_pstage_replica_id)(Wid rep_id);
Iteration PREFIX(current_iter)(void);

// specpriv_queue.cpp
PREFIX(queue)* PREFIX(create_queue)(uint32_t N, uint32_t M);
void           PREFIX(produce)(PREFIX(queue)*, int64_t value);
void           PREFIX(produce_replicated)(PREFIX(queue)*, int64_t value);
int64_t        PREFIX(consume)(PREFIX(queue)* specpriv_queue);
int64_t        PREFIX(consume_replicated)(PREFIX(queue)* specpriv_queue);
void           PREFIX(flush)(PREFIX(queue)* specpriv_queue);
void           PREFIX(clear)(PREFIX(queue)* specpriv_queue);
void           PREFIX(reset_queue)(PREFIX(queue)* specpriv_queue);
void           PREFIX(free_queue)(PREFIX(queue)* specpriv_queue);

// speculation.cpp
void      PREFIX(predict)(uint64_t observed, uint64_t expected);
Iteration PREFIX(last_committed)(void);
Iteration PREFIX(misspec_iter)(void);
void      PREFIX(misspec)(const char* msg);
void      PREFIX(recovery_finished)(Exit e);

void     PREFIX(loop_invocation)(void);
void     PREFIX(loop_exit)(void);
void     PREFIX(push_context)(const uint32_t ctxt);
void     PREFIX(pop_context)(void);
uint64_t PREFIX(get_context)(void);

// stratety.cpp
void PREFIX(inform_strategy)(unsigned num_workers, unsigned num_stages, ...);
void PREFIX(cleanup_strategy)(void);

// smtx/smtx.cpp
void PREFIX(init)();
void PREFIX(fini)();

// smtx/memops.cpp
void PREFIX(ver_read1)(int8_t* ptr);
void PREFIX(ver_read2)(int8_t* ptr);
void PREFIX(ver_read4)(int8_t* ptr);
void PREFIX(ver_read8)(int8_t* ptr);
void PREFIX(ver_read)(int8_t* ptr, uint32_t size);

void PREFIX(ver_write1)(int8_t* ptr);
void PREFIX(ver_write2)(int8_t* ptr);
void PREFIX(ver_write4)(int8_t* ptr);
void PREFIX(ver_write8)(int8_t* ptr);
void PREFIX(ver_write)(int8_t* ptr, uint32_t size);

void PREFIX(ver_memmove)(int8_t* write_ptr, uint32_t size, int8_t* read_ptr);

// smtx/malloc.cpp
void* PREFIX(ver_malloc)(size_t size);
void* PREFIX(ver_calloc)(size_t num, size_t size);
void* PREFIX(ver_realloc)(void* ptr, size_t size);
void  PREFIX(ver_free)(void* ptr);

void* PREFIX(malloc)(size_t size);
void* PREFIX(calloc)(size_t num, size_t size);
void* PREFIX(realloc)(void* ptr, size_t size);
void  PREFIX(free)(void* ptr);

// smtx/prediction.cpp
void PREFIX(init_predictors)(unsigned loop_invariant_loads, unsigned linear_predictable_loads, unsigned contexts, ...);
void PREFIX(fini_predictors)();
void PREFIX(check_loop_invariant)(unsigned load_id, uint64_t context, uint64_t* read_ptr, uint32_t read_size);
void PREFIX(register_loop_invariant_buffer)(unsigned load_id, uint64_t context, int64_t* read_ptr, uint32_t read_size);
void PREFIX(check_and_register_linear_predictor)(bool valid, unsigned load_id, uint64_t context, int64_t* read_ptr, uint32_t read_size);

// smtx/separation.cpp
void PREFIX(separation_init)(unsigned num_non_versioned_heaps, unsigned num_versioned_heaps);
void PREFIX(separation_fini)(unsigned num_non_versioned_heaps, unsigned num_versioned_heaps);
void PREFIX(clear_separation_heaps)();

void PREFIX(register_unclassified)(unsigned num, ...);
void PREFIX(register_versioned_unclassified)(unsigned num, ...);
void PREFIX(register_ro)(unsigned num, ...);
void PREFIX(register_versioned_ro)(unsigned num, ...);
void PREFIX(register_nrbw)(unsigned num, ...);
void PREFIX(register_versioned_nrbw)(unsigned num, ...);
void PREFIX(register_stage_private)(unsigned stage, unsigned num, ...);
void PREFIX(register_versioned_stage_private)(unsigned stage, unsigned num, ...);

void* PREFIX(separation_malloc)(size_t size, unsigned heap);
void* PREFIX(separation_calloc)(size_t num, size_t size, unsigned heap);
void* PREFIX(separation_realloc)(void* ptr, size_t size, unsigned heap);
void  PREFIX(separation_free)(void* ptr, unsigned heap);

void* PREFIX(ver_separation_malloc)(size_t size, unsigned heap);
void* PREFIX(ver_separation_calloc)(size_t num, size_t size, unsigned heap);
void* PREFIX(ver_separation_realloc)(void* ptr, size_t size, unsigned heap);
void  PREFIX(ver_separation_free)(void* ptr, unsigned heap);

void     PREFIX(push_separation_alloc_context)(uint32_t ctxt);
void     PREFIX(pop_separation_alloc_context)(void);
uint32_t PREFIX(get_separation_alloc_context)(void);

// debug.cpp
size_t __specpriv_debugprintf(const char* format, ...);
void   __specpriv_roidebug_init();

// profile
void PREFIX(count_skippables)();
void PREFIX(count_nonskippables)();

}

}
