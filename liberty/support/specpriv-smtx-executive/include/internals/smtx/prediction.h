#include <stddef.h>
#include <stdint.h>

namespace specpriv_smtx
{

extern volatile uint8_t* good_to_go;

// PREFIX(join_children)() call this function

void reset_predictors();

// PREFIX(init_predictors)() call this function
void init_loop_invariant_buffer(unsigned loads, unsigned contexts);

// commit() calls this at the end of first iteration
void update_loop_invariant_buffer();  

// PERFIX(begin_iter())() function calls this at the beginning of every stage 
void update_loop_invariants();

// PREFIX(end_iter)() function calls this at the end of iteration of every stage
void update_shadow_for_loop_invariants();

// try_commit() calls this at the end of non-zero iteration
bool verify_loop_invariants();

// try_commit() call this to update its local memory even when checking is not required
void try_commit_update_loop_invariants(int8_t* addr, int8_t* data, int8_t* shadow, size_t size);

// PREFIX(init_predictors)() call this function
void init_linear_predictors(unsigned loads, unsigned contexts);

// PERFIX(begin_iter())() function calls this at the beginning of every stage 
void update_linear_predicted_values();

// PREFIX(end_iter)() function calls this at the end of iteration of every stage
void update_shadow_for_linear_predicted_values();

// try_commit() calls this at the end of non-zero iteration
bool verify_linear_predicted_values();

// try_commit() call this to update its local memory even when checking is not required
void try_commit_update_linear_predicted_values(int8_t* addr, int8_t* data, int8_t* shadow, size_t size);
}
