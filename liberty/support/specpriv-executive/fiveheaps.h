#ifndef LLVM_LIBERTY_SPEC_PRIV_EXECUTIVE_HEAPS_H
#define LLVM_LIBERTY_SPEC_PRIV_EXECUTIVE_HEAPS_H

#include "heap.h"
#include "types.h"
#include "redux.h"

void __specpriv_initialize_main_heaps(void);
void __specpriv_initialize_worker_heaps(void);


void __specpriv_destroy_main_heaps(void);
void __specpriv_destroy_worker_heaps(void);

void *__specpriv_alloc_meta(Len size);
void __specpriv_free_meta(void *ptr);


void *__specpriv_alloc_shared(Len size, SubHeap subheap);
void __specpriv_free_shared(void *ptr);
void *__specpriv_alloc_ro(Len size);
void __specpriv_free_ro(void *ptr);
void *__specpriv_alloc_local(Len size, SubHeap subheap);
void __specpriv_free_local(void *ptr);
void *__specpriv_alloc_priv(Len size, SubHeap subheap);
void __specpriv_free_priv(void *ptr);

void *__specpriv_alloc_unclassified(Len size);
void __specpriv_free_unclassified(void *ptr);

void *__specpriv_alloc_redux(Len size, SubHeap subheap, ReductionType type);
void __specpriv_free_redux(void *ptr);

void *__specpriv_alloc_worker_redux(Len size, SubHeap subheap);
void __specpriv_free_worker_redux(void *ptr);

void __specpriv_reset_local(void);
unsigned __specpriv_num_local(void);


// Access the list of all reduction objects.
ReductionInfo *__specpriv_first_reduction_info(void);
ReductionInfo *__specpriv_last_reduction_info(void);

// Determine if there is a single reduction type which
// applies universally to all reduction objects.
ReductionType __specpriv_has_universal_reduction_type(void);

// Determine the size allocated in the private
// and redux heaps BEFORE the invocation began.
unsigned __specpriv_sizeof_private(void);
unsigned __specpriv_sizeof_redux(void);

uint64_t __specpriv_sizeof_private_subheap(SubHeap subheap);
uint64_t __specpriv_sizeof_redux_subheap(SubHeap subheap);

// At any time, one of the two is committed, and one is partial

void __specpriv_fiveheaps_begin_invocation(void);

#endif

