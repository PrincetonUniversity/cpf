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
void *__specpriv_alloc_ro(Len size, SubHeap subheap);
void __specpriv_free_ro(void *ptr);
void *__specpriv_alloc_local(Len size, SubHeap subheap);
void __specpriv_free_local(void *ptr);
void *__specpriv_alloc_priv(Len size, SubHeap subheap);
void __specpriv_free_priv(void *ptr);

void *__specpriv_alloc_unclassified(Len size);
void __specpriv_free_unclassified(void *ptr);

void *__specpriv_alloc_redux(Len size, SubHeap subheap, ReductionType type,
                             void *depAU, Len depSize, uint8_t depType);
void __specpriv_free_redux(void *ptr);

void *__specpriv_alloc_worker_redux(Len size);
void __specpriv_free_worker_redux(void *ptr);

void __specpriv_reset_local(void);
void __specpriv_add_num_local(int n);
int __specpriv_num_local(void);


ReductionInfo *__specpriv_first_reduction_info(void);
void __specpriv_set_first_reduction_info(ReductionInfo *frI);

// Determine the size allocated in the private
// and redux heaps BEFORE the invocation began.
unsigned __specpriv_sizeof_private(void);
unsigned __specpriv_sizeof_redux(void);

unsigned __specpriv_sizeof_ro(void);
void __specpriv_set_sizeof_private(unsigned sp);
void __specpriv_set_sizeof_redux(unsigned sr);
void __specpriv_set_sizeof_ro(unsigned sr);

// At any time, one of the two is committed, and one is partial

void __specpriv_fiveheaps_begin_invocation(void);

#endif

