#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "config.h"
#include "constants.h"
#include "fiveheaps.h"
#include "pcb.h"
#include "checkpoint.h"

#include "api.h"


// One big shared heap which contains all
// the metadata for the running system.
static Heap         meta;
static MappedHeap   *mapped_meta;


// These heaps are mapped within the main process.
// shared - shared by main and workers
// ro     - shared by main and workers
// priv0  - CoW by workers
// redux0 - private to main; worker has corresponding private heap.
static Heap         shared, ro;
static MappedHeap   mshared, mro, mpriv0, mredux0;


// These heaps are 'owned' by each worker
// redux  - initially zero, accumulates sum reductions
// shadow - metadata for private AUs, determines if a privacy violation occurs.
// local  - holds short-lived objects
static Heap redux[MAX_WORKERS];
static MappedHeap myShadow, myLocal, myRedux;

/*
// The main process may also read the reduction heaps
// for each of the workers.
static MappedHeap workerRedux[MAX_WORKERS];
*/

// The how many short-lived AUs have been allocated?
// Should be zero at beginning/end of iteration
static unsigned numLocalAUs;

// Allow us to enumerate all reduction AUs at runtime.
static ReductionInfo *first_reduction_info,
                     *last_reduction_info;
// If the program only uses one kind of reduction
// for all objects allocated from the reduction
// heap, mark that here.  Set this to NotReduction
// if more than one distinct type of reduction
// is present.
static ReductionType universal_reduction_type;

static Len sizeof_private, sizeof_redux;

void *__specpriv_alloc_meta(Len len)
{
  return heap_alloc(mapped_meta,len);
}

void __specpriv_free_meta(void *ptr)
{
  heap_free(mapped_meta, ptr);
}

void __specpriv_initialize_main_heaps(void)
{
  const Wid numWorkers = __specpriv_num_workers();

  // Allocate a metadata heap, which will hold
  // the parallel control block, and the list of
  // redux objects.
  heap_init(&meta, "meta", HEAP_SIZE, (void*) META_ADDR, 0);
  {
    MappedHeap tmp_meta;
    mapped_heap_init(&tmp_meta);
    heap_map_shared(&meta, &tmp_meta);
    heap_reset(&tmp_meta);
    mapped_meta = (MappedHeap*)heap_alloc(&tmp_meta, sizeof(MappedHeap));
    memcpy(mapped_meta, &tmp_meta, sizeof(MappedHeap));
  }

  // Allocate a parallel control block in the metadata heap.
  ParallelControlBlock *pcb = __specpriv_get_pcb();

  // Create heaps for the main process.
  heap_init(&shared, "shared",  HEAP_SIZE, (void*) (SHARED_ADDR), 0);
  heap_init(&ro,     "ro",      HEAP_SIZE, (void*) (RO_ADDR    ), 0);

  // Map these in the main process
  mapped_heap_init(&mshared);
  mapped_heap_init(&mro);
  mapped_heap_init(&mpriv0);
  mapped_heap_init(&mredux0);
  sizeof_private = sizeof_redux = 0;

  heap_map_shared(&shared, &mshared);
  heap_reset(&mshared);
  heap_map_cow(&ro,     &mro);
  heap_reset(&mro);

  // Map the /right/ version of the private, redux heaps.
  heap_map_shared( &pcb->checkpoints.main_checkpoint->heap_priv, &mpriv0);
  heap_reset(&mpriv0);
  heap_map_shared( &pcb->checkpoints.main_checkpoint->heap_redux, &mredux0);
  heap_reset(&mredux0);

  // Create reduction heaps for each worker.
  Wid i;
  for(i=0; i<numWorkers; ++i)
    heap_init(&redux[i],  "redux-i",  HEAP_SIZE, (void*) (REDUX_ADDR), i);

  // Empty list of reduction aus.
  first_reduction_info = last_reduction_info = 0;
  universal_reduction_type = NotReduction;
}

void __specpriv_destroy_main_heaps(void)
{
  const Wid numWorkers = __specpriv_num_workers();

  // Clear the list.
  first_reduction_info = last_reduction_info = 0;

  // Destroy the reduction heaps
  Wid i;
  for(i=0; i<numWorkers; ++i)
    heap_fini(&redux[i]);

  // Unmap main heaps
  heap_unmap(&mshared);
  heap_unmap(&mro);
  heap_unmap(&mpriv0);
  heap_unmap(&mredux0);

  // Destroy main heaps.
  heap_fini(&shared);
  heap_fini(&ro);

  __specpriv_destroy_pcb();

  heap_unmap(mapped_meta);
  heap_fini(&meta);
}

void __specpriv_worker_unmap_private(void)
{
  if( mpriv0.heap )
    heap_unmap( &mpriv0 );
}

void __specpriv_worker_remap_private(void)
{
  __specpriv_worker_unmap_private();

  ParallelControlBlock *pcb = __specpriv_get_pcb();
  heap_map_cow( &pcb->checkpoints.main_checkpoint->heap_priv, &mpriv0 );
}

void __specpriv_fiveheaps_begin_invocation(void)
{
  sizeof_private = heap_used( &mpriv0 );
  sizeof_redux = heap_used( &mredux0 );
}

uint64_t __specpriv_sizeof_private_subheap(SubHeap subheap)
{
  if( subheap < 0 ) subheap = 0;
  return subheap_used( &mpriv0, subheap );
}

uint64_t __specpriv_sizeof_redux_subheap(SubHeap subheap)
{
  if( subheap < 0 ) subheap = 0;
  return subheap_used( &myRedux, subheap );
}

void __specpriv_initialize_worker_heaps(void)
{
  const Wid myWorkerId = __specpriv_my_worker_id();

  // re-map the committed version of heap 'priv' as copy-on-write
  __specpriv_worker_remap_private();

  // re-map a new independent heap as my 'redux' heap.
  heap_unmap(&mredux0);
  mapped_heap_init(&myRedux);
  heap_map_shared(&redux[myWorkerId], &myRedux);

  // map my 'shadow', and 'local' heaps
  mapped_heap_init(&myShadow);
  mapped_heap_init(&myLocal);
  heap_map_anon(HEAP_SIZE, (void*) (SHADOW_ADDR), &myShadow);
  heap_map_anon(HEAP_SIZE, (void*) (LOCAL_ADDR ), &myLocal);
}

void __specpriv_destroy_worker_heaps(void)
{
  heap_unmap(&mshared);
  heap_unmap(&mro);
  heap_unmap(&myLocal);
  heap_unmap(&myRedux);
  heap_unmap(&myShadow);
}

//------------------------------------------------------------------
// Heap allocation/deallocation routines

// Allocate memory in the shared heap
void *__specpriv_alloc_shared(Len size, SubHeap subheap)
{
  assert( __specpriv_i_am_main_process() );
  if( subheap < 0 ) subheap = 0;
  return heap_alloc_subheap(&mshared, size, subheap);
}

// Free memory to the shared heap
void __specpriv_free_shared(void *ptr)
{
  assert( __specpriv_i_am_main_process() );
  heap_free(&mshared,ptr);
}

void *__specpriv_alloc_ro(Len size)
{
  assert( __specpriv_i_am_main_process() );
  return heap_alloc(&mro, size);
}

void __specpriv_free_ro(void *ptr)
{
  assert( __specpriv_i_am_main_process() );
  heap_free(&mro, ptr);
}

void *__specpriv_alloc_local(Len size, SubHeap subheap)
{
  if( __specpriv_i_am_main_process() )
    // during recovery
    return __specpriv_alloc_shared(size, subheap);

  if( subheap < 0 ) subheap = 0;
  void *p = heap_alloc_subheap(&myLocal, size, subheap);
  ++numLocalAUs;
  return p;
}

void __specpriv_free_local(void *ptr)
{
  if( __specpriv_i_am_main_process() )
  {
    // during recovery
    __specpriv_free_shared(ptr);
    return;
  }

  --numLocalAUs;
  heap_free(&myLocal,ptr);
}

void *__specpriv_alloc_priv(Len size, SubHeap subheap)
{
  assert( __specpriv_i_am_main_process() );
  if( subheap < 0 ) subheap = 0;
  return heap_alloc_subheap(&mpriv0, size, subheap);
}

void __specpriv_free_priv(void *ptr)
{
  assert( __specpriv_i_am_main_process() );
  heap_free(&mpriv0, ptr);
}

void *__specpriv_alloc_redux(Len size, SubHeap subheap, ReductionType type)
{
  assert( __specpriv_i_am_main_process() );

  // Record info about this AU.
  ReductionInfo *info = (ReductionInfo*)__specpriv_alloc_meta( sizeof(ReductionInfo) );
  info->next = 0;
  info->size = size;
  info->type = type;
  if( subheap < 0 ) subheap = 0;
  info->au = heap_alloc_subheap(&mredux0, size, subheap);

  // Update the list.
  if( last_reduction_info )
    last_reduction_info->next = info;

  last_reduction_info = info;

  // First one?
  if( !first_reduction_info )
  {
    // Update front of list.
    first_reduction_info = info;

    // Update universal reduction type.
    universal_reduction_type = type;
  }
  // Ensure universal reduction type is
  // consistent for all reduction objects.
  else if( universal_reduction_type != type )
  {
    universal_reduction_type = NotReduction;
  }

  return info->au;
}

void *__specpriv_alloc_worker_redux(Len size, SubHeap subheap)
{
  assert( ! __specpriv_i_am_main_process() );
  if( subheap < 0 ) subheap = 0;
  return heap_alloc_subheap(&myRedux, size, subheap);
}

void __specpriv_free_redux(void *ptr)
{
  assert( __specpriv_i_am_main_process() );
  heap_free(&mredux0, ptr);
}

void __specpriv_reset_local(void)
{
  heap_reset(&myLocal);
  numLocalAUs = 0;
}

unsigned __specpriv_num_local(void)
{
  return numLocalAUs;
}

ReductionInfo *__specpriv_first_reduction_info(void)
{
  return first_reduction_info;
}
ReductionInfo *__specpriv_last_reduction_info(void)
{
  return last_reduction_info;
}


ReductionType __specpriv_has_universal_reduction_type(void)
{
  return universal_reduction_type;
}

Len __specpriv_sizeof_private(void)
{
  return sizeof_private;
}
Len __specpriv_sizeof_redux(void)
{
  return sizeof_redux;
}

void *__specpriv_alloc_unclassified(Len size)
{
  assert( 0 && "Alloc unclassified?!");
}

void __specpriv_free_unclassified(void *ptr)
{
//  assert( 0 && "Free unclassified!?");
}



