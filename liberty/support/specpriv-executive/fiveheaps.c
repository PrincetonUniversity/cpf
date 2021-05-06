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
static MappedHeap   mshared, mro, mpriv0, mredux0, mkillpriv0, msharepriv0;


// These heaps are 'owned' by each worker
// redux  - initially zero, accumulates sum reductions
// shadow - metadata for private AUs, determines if a privacy violation occurs.
// local  - holds short-lived objects
static Heap redux[MAX_WORKERS];
static Heap local;
static MappedHeap myShadow, myLocal, myRedux, myShareShadow;

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


static Len sizeof_private, sizeof_killprivate, sizeof_shareprivate, sizeof_redux;
static Len sizeof_ro;
static Len sizeof_local;

void __specpriv_reset_reduction()
{
  first_reduction_info = last_reduction_info = 0;
  return;
}

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
  DEBUG(printf("INITIALIZE MAIN HEAP\n"));
  const Wid numWorkers = __specpriv_num_workers();

  // Allocate a metadata heap, which will hold
  // the parallel control block, and the list of
  // redux objects.
  heap_init(&meta, "meta", HEAP_SIZE, (void*) META_ADDR, 0);
  {
    MappedHeap tmp_meta;
    mapped_heap_init(&tmp_meta);
    heap_map_shared(&meta, &tmp_meta);
    mapped_meta = (MappedHeap*)heap_alloc(&tmp_meta, sizeof(MappedHeap));
    memcpy(mapped_meta, &tmp_meta, sizeof(MappedHeap));
  }

  // Allocate a parallel control block in the metadata heap.
  ParallelControlBlock *pcb = __specpriv_get_pcb();

  // Create heaps for the main process.
  heap_init(&shared, "shared",  HEAP_SIZE, (void*) (SHARED_ADDR), 0);
  heap_init(&ro,     "ro",      HEAP_SIZE, (void*) (RO_ADDR    ), 0);
  heap_init(&local,  "local",   HEAP_SIZE, (void*) (LOCAL_ADDR ), 0);

  // Map these in the main process
  mapped_heap_init(&mshared);
  mapped_heap_init(&mro);
  mapped_heap_init(&mpriv0);
  mapped_heap_init(&mkillpriv0);
  mapped_heap_init(&msharepriv0);
  mapped_heap_init(&mredux0);
  mapped_heap_init(&myLocal);
  sizeof_private = sizeof_redux = 0;

  heap_map_shared(&shared, &mshared);

  // ro needs to be shared, up until invocation when spawning once
  heap_map_shared(&ro,     &mro);
  //heap_map_cow(&ro,     &mro);

  // local heap needs to be shared until invocation
  heap_map_shared(&local,  &myLocal);

  // Map the /right/ version of the private, redux heaps.
  heap_map_shared( &pcb->checkpoints.main_checkpoint->heap_priv, &mpriv0);
  heap_map_shared( &pcb->checkpoints.main_checkpoint->heap_killpriv, &mkillpriv0);
  heap_map_shared( &pcb->checkpoints.main_checkpoint->heap_sharepriv, &msharepriv0);
  heap_map_shared( &pcb->checkpoints.main_checkpoint->heap_redux, &mredux0);

  // Create reduction heaps for each worker.
  Wid i;
  for(i=0; i<numWorkers; ++i)
    heap_init(&redux[i],  "redux-i",  HEAP_SIZE, (void*) (REDUX_ADDR), i);

  // Empty list of reduction aus.
  first_reduction_info = last_reduction_info = 0;
}

void __specpriv_destroy_main_heaps(void)
{
  DEBUG(printf("DESTROY MAIN HEAP\n"));
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
  heap_unmap(&mkillpriv0);
  heap_unmap(&msharepriv0);
  heap_unmap(&mredux0);
  heap_unmap(&myLocal);

  // Destroy main heaps.
  heap_fini(&shared);
  heap_fini(&ro);
  heap_fini(&local);

  __specpriv_destroy_pcb();

  heap_unmap(mapped_meta);
  heap_fini(&meta);
}

void __specpriv_worker_unmap_ro(void)
{
  if( mro.heap )
    heap_unmap( &mro );
}

void __specpriv_worker_remap_ro(void)
{
  __specpriv_worker_unmap_ro();

  heap_map_read_only( &ro, &mro );
  if( sizeof_ro )
    heap_alloc( &mro, sizeof_ro );
}

void __specpriv_worker_unmap_local(void)
{
  if ( myLocal.heap )
    heap_unmap( &myLocal );
}

void __specpriv_worker_remap_local(void)
{
  Wid myWid = __specpriv_my_worker_id();
  __specpriv_worker_unmap_local();

  heap_map_cow( &local, &myLocal );
  if ( sizeof_local )
  {
    heap_alloc( &myLocal, sizeof_local );
    DEBUG(printf("Worker %u preserving %u bytes from previous map in local\n", myWid, sizeof_local););
    DEBUG(printf("Local heap is now %u bytes\n", heap_used(&myLocal)););
    DEBUG(printf("Next alloc to local heap should return %p\n", myLocal.next); fflush(stdout););
  }
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
  if( sizeof_private )
    heap_alloc( &mpriv0, sizeof_private );
}

void __specpriv_worker_unmap_killprivate( void )
{
  if ( mkillpriv0.heap )
    heap_unmap( &mkillpriv0 );
}

void __specpriv_worker_remap_killprivate( void )
{
  __specpriv_worker_unmap_killprivate();

  ParallelControlBlock *pcb = __specpriv_get_pcb();
  heap_map_cow( &pcb->checkpoints.main_checkpoint->heap_killpriv, &mkillpriv0 );
  if ( sizeof_killprivate )
    heap_alloc( &mkillpriv0, sizeof_killprivate );
}

void __specpriv_worker_unmap_shareprivate( void )
{
  if ( msharepriv0.heap )
    heap_unmap( &msharepriv0 );
}

void __specpriv_worker_remap_shareprivate( void )
{
  __specpriv_worker_unmap_shareprivate();

  ParallelControlBlock *pcb = __specpriv_get_pcb();
  heap_map_cow( &pcb->checkpoints.main_checkpoint->heap_sharepriv, &msharepriv0 );
  if ( sizeof_shareprivate )
    heap_alloc( &msharepriv0, sizeof_shareprivate );
}

void __specpriv_fiveheaps_begin_invocation(void)
{
  sizeof_private = heap_used( &mpriv0 );
  sizeof_killprivate = heap_used( &mkillpriv0 );
  sizeof_shareprivate = heap_used( &msharepriv0 );
  sizeof_redux = heap_used( &mredux0 );
  sizeof_ro = heap_used( &mro );
  sizeof_local = heap_used( &myLocal );
}

void __specpriv_initialize_worker_heaps(void)
{
  //const Wid myWorkerId = __specpriv_my_worker_id();

  // re-map the committed version of heap 'priv' as copy-on-write
  __specpriv_worker_remap_private();

  __specpriv_worker_remap_killprivate();

  __specpriv_worker_remap_shareprivate();

  // re-map the committed version of heap 'ro' as copy-on-write
  // added due to process spawning once at startup
  __specpriv_worker_remap_ro();

  // remap local heap to deal with weird "local" variables
  __specpriv_worker_remap_local();

   const Wid myWorkerId = __specpriv_my_worker_id();

  // re-map my a new independent heap as my 'redux' heap.
  if (mredux0.heap)
    heap_unmap(&mredux0);
  mapped_heap_init(&myRedux);
  heap_map_shared(&redux[myWorkerId], &myRedux);
  //ParallelControlBlock *pcb = __specpriv_get_pcb();
  //heap_map_cow( &pcb->checkpoints.main_checkpoint->heap_redux, &mredux0 );
  if( sizeof_redux )
    heap_alloc(&myRedux, sizeof_redux);
    //heap_alloc(&mredux0, sizeof_redux);

  // map my 'shadow', and 'local' heaps
  mapped_heap_init(&myShadow);
  /* mapped_heap_init(&myLocal); */
  heap_map_anon(HEAP_SIZE, (void*) (SHADOW_ADDR), &myShadow);
  /* heap_map_anon(HEAP_SIZE, (void*) (LOCAL_ADDR ), &myLocal); */
  mapped_heap_init(&myShareShadow);
  /* mapped_heap_init(&myLocal); */
  heap_map_anon(HEAP_SIZE, (void*) (SHARESHADOW_ADDR), &myShareShadow);

  // initialize shadow memory of share-privs with the original data at loop
  // invocation
  const unsigned sharelen = __specpriv_sizeof_shareprivate();
  if (sharelen > 0)
    memcpy( (uint8_t *) SHARESHADOW_ADDR, (uint8_t *) SHAREPRIV_ADDR, sharelen );

}

void __specpriv_destroy_worker_heaps(void)
{
  //heap_unmap(&mpriv0);
  //heap_unmap(&mredux0);
  //heap_unmap(&mshared);
  //heap_unmap(&mro);

  //heap_unmap(&myLocal);
  heap_unmap(&myRedux);
  heap_unmap(&myShadow);
  heap_unmap(&myShareShadow);
}

//------------------------------------------------------------------
// Heap allocation/deallocation routines

// Allocate memory in the shared heap
void *__specpriv_alloc_shared(Len size, SubHeap subheap)
{
  assert( __specpriv_i_am_main_process() );
  return heap_alloc(&mshared, size);
}

// Free memory to the shared heap
void __specpriv_free_shared(void *ptr)
{
  assert( __specpriv_i_am_main_process() );
  heap_free(&mshared,ptr);
}

void *__specpriv_alloc_ro(Len size, SubHeap subheap)
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
  /* if( __specpriv_i_am_main_process() ) */
  /* { */
  /*   DEBUG(printf("Main allocated %u bytes in local\n", size);); */
  /*   return __specpriv_alloc_shared(size, subheap); */
  /* } */
  Wid myWid = __specpriv_my_worker_id();

  /* DEBUG(printf("Worker %u preparing to alloc at %p\n", myWid, myLocal.next);); */
  void *p = heap_alloc(&myLocal, size);
  /* DEBUG(printf("Worker %u allocated %u in local at %p\n", myWid, size, p); fflush(stdout);); */
  ++numLocalAUs;
  return p;
}

void __specpriv_free_local(void *ptr)
{
  /* if( __specpriv_i_am_main_process() ) */
  /* { */
    // during recovery
    /* __specpriv_free_shared(ptr); */
    /* return; */
  /* } */
  Wid myWid = __specpriv_my_worker_id();

  /* DEBUG(printf("Worker %u freed %p in local\n", myWid, ptr); fflush(stdout);); */
  --numLocalAUs;
  heap_free(&myLocal,ptr);
}

void *__specpriv_alloc_priv(Len size, SubHeap subheap)
{
  assert( __specpriv_i_am_main_process() );
  return heap_alloc(&mpriv0, size);
}

void __specpriv_free_priv(void *ptr)
{
  assert( __specpriv_i_am_main_process() );
  heap_free(&mpriv0, ptr);
}

void *__specpriv_alloc_killpriv(Len size, SubHeap subheap)
{
  assert( __specpriv_i_am_main_process() );
  void *p =  heap_alloc( &mkillpriv0, size );
  DEBUG(printf("Allocating %u at %p to kill priv heap\n", size, p););
  return p;
}

void __specpriv_free_killpriv( void *ptr )
{
  assert( __specpriv_i_am_main_process() );
  DEBUG(printf("Freeing at %p from kill priv heap\n", ptr););
  heap_free( &mkillpriv0, ptr );
}

void *__specpriv_alloc_sharepriv(Len size, SubHeap subheap)
{
  assert( __specpriv_i_am_main_process() );
  void *p =  heap_alloc( &msharepriv0, size );
  DEBUG(printf("Allocating %u at %p to share priv heap\n", size, p););
  return p;
}

void __specpriv_free_sharepriv( void *ptr )
{
  assert( __specpriv_i_am_main_process() );
  DEBUG(printf("Freeing at %p from share priv heap\n", ptr););
  heap_free( &msharepriv0, ptr );
}

void *__specpriv_alloc_redux(Len size, SubHeap subheap, ReductionType type,
                             uint8_t reg, void *depAU, Len depSize,
                             uint8_t depType) {

  DEBUG(printf("Allocate redux alloc\n"));
  DEBUG(printf("ReductionType:%hhu, len:%u, dep_len:%u\n",type, size, depSize));

  assert( __specpriv_i_am_main_process() );

  // Record info about this AU.
  ReductionInfo *info = (ReductionInfo*)__specpriv_alloc_meta( sizeof(ReductionInfo) );
  info->next = 0;
  info->size = size;
  info->type = type;
  info->au = heap_alloc(&mredux0, size);
  info->reg = reg;
  info->depAU = depAU;
  info->depSize = depSize;
  info->depType = depType;

  // Update the list.
  // append new info at the start so that dependent min/max redux are processed
  // before the redux they depend upon (which calls __specpriv_alloc_redux
  // first)
  if (first_reduction_info)
    info->next = first_reduction_info;

  first_reduction_info = info;

  if (!last_reduction_info)
    last_reduction_info = info;

  return info->au;
}

void *__specpriv_alloc_worker_redux(Len size)
{
  assert( ! __specpriv_i_am_main_process() );
  return heap_alloc(&myRedux, size);
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

void __specpriv_reset_num_local(void)
{
  numLocalAUs = 0;
}

int __specpriv_num_local(void)
{
  return numLocalAUs;
}

void __specpriv_add_num_local(int n)
{
  numLocalAUs += n;
}

ReductionInfo *__specpriv_first_reduction_info(void)
{
  return first_reduction_info;
}

void __specpriv_set_first_reduction_info(ReductionInfo *frI)
{
  first_reduction_info = frI;
}

Len __specpriv_sizeof_private(void)
{
  return sizeof_private;
}

Len __specpriv_sizeof_killprivate(void)
{
  return sizeof_killprivate;
}

Len __specpriv_sizeof_shareprivate(void)
{
  return sizeof_shareprivate;
}

Len __specpriv_sizeof_redux(void)
{
  return sizeof_redux;
}

Len __specpriv_sizeof_ro(void)
{
  return sizeof_ro;
}

Len __specpriv_sizeof_local(void)
{
  return sizeof_local;
}

void __specpriv_set_sizeof_private(Len sp)
{
  sizeof_private = sp;
}

void __specpriv_set_sizeof_killprivate(Len sp)
{
  sizeof_killprivate = sp;
}

void __specpriv_set_sizeof_shareprivate(Len sp)
{
  sizeof_shareprivate = sp;
}

void __specpriv_set_sizeof_redux(Len sr)
{
  sizeof_redux = sr;
}

void __specpriv_set_sizeof_ro(Len sr)
{
  sizeof_ro = sr;
}

void __specpriv_set_sizeof_local(Len sr)
{
  sizeof_local = sr;
}

void *__specpriv_alloc_unclassified(Len size)
{
  assert( 0 && "Alloc unclassified?!");
}

void __specpriv_free_unclassified(void *ptr)
{
//  assert( 0 && "Free unclassified!?");
}



