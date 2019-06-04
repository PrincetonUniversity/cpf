#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "config.h"

#include "pcb.h"
#include "checkpoint.h"
#include "redux.h"
#include "api.h"
#include "private.h"
#include "timer.h"
#include "fiveheaps.h"

// Determine the amount of memory (in bytes) used by the main checkpoint.
// Not completely accurate, ignores small sources of memory consumption,
// and instead focuses on the contribution of each heap...
static uint64_t __specpriv_count_memory_used_by_main_checkpoint(void)
{
  uint64_t size_private = __specpriv_sizeof_private();
  uint64_t size_shadow = size_private;
  uint64_t size_redux = __specpriv_sizeof_redux();

  return size_private + size_shadow + size_redux;
}

// Determine the amount of memory (in bytes) used by this checkpoint.
// Not completely accurate, ignores small sources of memory consumption,
// and instead focuses on the contribution of each heap...
static uint64_t __specpriv_count_memory_used_by_checkpoint(Checkpoint *chkpt)
{
  uint64_t size_shadow = 0;
  if( chkpt->shadow_highest_exclusive > chkpt->shadow_lowest_inclusive )
    size_shadow = (chkpt->shadow_highest_exclusive - chkpt->shadow_lowest_inclusive);

  uint64_t size_private = size_shadow;
  uint64_t size_redux = chkpt->redux_used;

  return size_private + size_shadow + size_redux;
}

static void acquire_lock(unsigned *lock)
{
  while( ! __sync_bool_compare_and_swap(lock,0,1) )
    sleep(0);
}

static void release_lock(unsigned *lock)
{
  assert( __sync_bool_compare_and_swap(lock,1,0) );
}

static Bool list_empty(CheckpointList *list)
{
  return (list->first == 0);
}

static Checkpoint *pop_front(CheckpointList *list)
{
  Checkpoint *node = list->first;
  assert( node );

  list->first = node->next;
  if( list->first )
    list->first->prev = 0;
  else
    list->last = 0;

  node->next = node->prev = 0;
  return node;
}

static void push_back(CheckpointList *list, Checkpoint *node)
{
  node->next = 0;

  node->prev = list->last;
  if( node->prev )
    node->prev->next = node;

  list->last = node;

  if( !list->first )
    list->first = node;
}

// Assumes that the manager object is locked.
static Bool __specpriv_is_saturated(CheckpointManager *mgr)
{
  unsigned num=0;
  uint64_t size = __specpriv_count_memory_used_by_main_checkpoint();

  for(Checkpoint *i=mgr->used.first; i; i=i->next, ++num)
    size += __specpriv_count_memory_used_by_checkpoint( i );

  // TODO: this does not account for the cost of the
  // per-worker priv, shadow, redux heaps.

  const Bool saturated = (num > 1) && (size > MAX_CHECKPOINT);
  DEBUG(printf("alloc-checkpoint: %u checkpoints use %lu / %lu\n",
    num, size, MAX_CHECKPOINT));

  return saturated;
}

// Assumes that the manager object has been locked!
Checkpoint *__specpriv_alloc_checkpoint(CheckpointManager *mgr)
{
  for(;;)
  {
    // If available, reuse an old checkpoint object.
    if( !list_empty(&mgr->free) )
    {
      Checkpoint *chkpt = pop_front( &mgr->free );
      chkpt->type = CL_Free;
      return chkpt;
    }

    // Impose a limit on number of checkpoint objects
    // which may be simulaneously live.
    if( ! __specpriv_is_saturated(mgr) )
      break;

    // Stall, waiting for a checkpoint to become available.

    release_lock( &mgr->lock );

    DEBUG(printf(" -> Saturated at %u checkpoint objects\n", mgr->total_checkpoint_objects));
    DEBUG(printf("Waiting for a checkpoint object: worker %u\n", __specpriv_my_worker_id()));
    ParallelControlBlock *pcb = __specpriv_get_pcb();
    Iteration currentIter = __specpriv_current_iter();
    if (!__specpriv_runOnEveryIter()) {
      const Wid numWorkers = __specpriv_num_workers();
      currentIter -= currentIter % numWorkers;
    }

    while( list_empty( &mgr->free ) && __specpriv_is_saturated(mgr) )
    {
      if( pcb->misspeculation_happened && pcb->misspeculated_iteration <= currentIter )
        return 0;

      struct timespec wt;
      wt.tv_sec = 0;
      wt.tv_nsec = 100000; // 100 microseconds
      nanosleep(&wt,0);

      __specpriv_commit_zero_or_more_checkpoints( mgr );
    }

    acquire_lock( &mgr->lock );
  }

  const unsigned name = mgr->total_checkpoint_objects++;
  Checkpoint *chkpt = (Checkpoint*)__specpriv_alloc_meta( sizeof(Checkpoint) );

  chkpt->lock = 0;
  chkpt->type = CL_Free;
  chkpt->iteration = -1;
  chkpt->num_workers = 0;
  chkpt->prev = chkpt->next = 0;

  for(Wid wid=0; wid<MAX_WORKERS; ++wid)
  {
    chkpt->io_events.lists[ wid ] = 0;
    chkpt->io_events.num[ wid ] = 0;
  }

  heap_init( &chkpt->heap_priv,   "chkpt-private", HEAP_SIZE, (void*)PRIV_ADDR,   name);
  heap_init( &chkpt->heap_shadow, "chkpt-shadow",  HEAP_SIZE, (void*)SHADOW_ADDR, name);
  heap_init( &chkpt->heap_redux,  "chkpt-redux",   HEAP_SIZE, (void*)REDUX_ADDR,  name);

  return chkpt;
}

void __specpriv_destroy_checkpoint(Checkpoint *chkpt)
{
  heap_fini( &chkpt->heap_redux );
  heap_fini( &chkpt->heap_shadow );
  heap_fini( &chkpt->heap_priv );

  __specpriv_free_meta( chkpt );
}

void __specpriv_init_checkpoint_list(CheckpointList *list)
{
  list->first = list->last = 0;
}

void __specpriv_init_checkpoint_manager(CheckpointManager *mgr)
{
  mgr->lock = 0;
  acquire_lock( &mgr->lock );

  mgr->total_checkpoint_objects = 0;

  __specpriv_init_checkpoint_list( &mgr->used );
  __specpriv_init_checkpoint_list( &mgr->free );

  mgr->main_checkpoint = __specpriv_alloc_checkpoint(mgr);
  assert( mgr->main_checkpoint && "This should never fail");

  mgr->main_checkpoint->type = CL_Main;

  release_lock( &mgr->lock );
}

void __specpriv_destroy_checkpoint_list(CheckpointList *list)
{
  for(Checkpoint *i=list->first; i; i=i->next)
    __specpriv_destroy_checkpoint(i);

  list->first = list->last = 0;
}

void __specpriv_destroy_checkpoint_manager(CheckpointManager *mgr)
{
  assert( mgr->lock == 0 );

  __specpriv_destroy_checkpoint_list( &mgr->used );
  __specpriv_destroy_checkpoint_list( &mgr->free );

  __specpriv_destroy_checkpoint( mgr->main_checkpoint );

  DEBUG(printf("At peak, there were %d checkpoints live.\n", mgr->total_checkpoint_objects));
  mgr->total_checkpoint_objects = 0;
}

static void __specpriv_initialize_partial_checkpoint(Checkpoint *partial, MappedHeap *shadow, MappedHeap *redux)
{
  // Initialize shadow, redux.

  const Len priv_used = __specpriv_sizeof_private();
  const Len redux_used= __specpriv_sizeof_redux();

  memset((void*)shadow->base, LIVE_IN, priv_used);

  partial->shadow_lowest_inclusive = (uint8_t*) (SHADOW_ADDR + (1UL<<POINTER_BITS));
  partial->shadow_highest_exclusive = (uint8_t*) (SHADOW_ADDR);

  // initialize with identity reductions in the checkpoint object
  //
  // initialize at first  all memory to zero.
  // Suitable for sum reduction and max reduction with unsigned.
  // For the other types of reduction special handling required.
  memset((void*)redux->base, 0, redux_used);

  ReductionInfo *info = __specpriv_first_reduction_info();
  for (; info; info = info->next) {
    DEBUG(printf(" initialize next info __specpriv_distill_worker_redux_into_partial\n"));
    void *native_au = info->au;
    void *dst_au = heap_translate(native_au, redux);

    if (info->depSize)
      continue;

    __specpriv_initialize_reductions(dst_au, info);
  }
}


Checkpoint *__specpriv_get_checkpoint_for_iter(CheckpointManager *mgr, Iteration iter)
{
  DEBUG(printf("Worker %u requestst checkpoint for iteration %d\n", __specpriv_my_worker_id(), iter));

#if (WHO_DOES_CHECKPOINTS & FASTEST_WORKER) != 0
  __specpriv_commit_zero_or_more_checkpoints( mgr );
#endif

  acquire_lock( &mgr->lock );
  // If one already exists, use it.
  if( !list_empty( &mgr->used ) )
    for(Checkpoint *cursor=mgr->used.first; cursor; cursor=cursor->next)
    {
      if( cursor->iteration == iter )
      {
        release_lock( &mgr->lock );
        DEBUG(printf("  -> old\n"));
        return cursor;
      }

      assert( cursor->iteration < iter );
    }

  // Otherwise, create and initialize a new checkpoint.
  Checkpoint *chkpt = __specpriv_alloc_checkpoint(mgr);
  if( !chkpt )
    return 0;

  chkpt->num_workers = 0;
  chkpt->type = CL_Partial;
  chkpt->iteration = iter;
  chkpt->redux_used = __specpriv_sizeof_redux();
  push_back( &mgr->used, chkpt );
  release_lock( &mgr->lock );

  DEBUG(printf("  -> new\n"));
  return chkpt;
}


static Bool __specpriv_combine_private(Checkpoint *older, Checkpoint *newer)
{
  Bool misspec = 0;

  // Map the private heaps
  MappedHeap commit_priv, partial_priv;
  mapped_heap_init( &commit_priv );
  mapped_heap_init( &partial_priv );
  heap_map_anywhere( &older->heap_priv, &commit_priv );
  heap_map_anywhere( &newer->heap_priv, &partial_priv );

  // Map the shadow heaps
  MappedHeap commit_shadow, partial_shadow;
  mapped_heap_init( &commit_shadow );
  mapped_heap_init( &partial_shadow );
  heap_map_anywhere( &older->heap_shadow, &commit_shadow );
  heap_map_anywhere( &newer->heap_shadow, &partial_shadow );

  // Combine them
  misspec |= __specpriv_distill_committed_private_into_partial(
    older, &commit_priv, &commit_shadow,
    newer, &partial_priv, &partial_shadow);

  if( misspec )
    newer->type = CL_Broken;

  // Unmap
  heap_unmap( &partial_shadow );
  heap_unmap( &commit_shadow );

  // Unmap
  heap_unmap( &partial_priv );
  heap_unmap( &commit_priv );

  return misspec;
}

static Bool __specpriv_combine_redux(Checkpoint *older, Checkpoint *newer)
{
  Bool misspec = 0;

  // Map the redux heaps
  MappedHeap commit_redux, partial_redux;
  mapped_heap_init( &commit_redux );
  mapped_heap_init( &partial_redux );
  heap_map_anywhere( &older->heap_redux, &commit_redux );
  heap_map_anywhere( &newer->heap_redux, &partial_redux );

  // combine them.
  misspec = __specpriv_distill_committed_redux_into_partial(
      &commit_redux, &partial_redux, older->lastUpdateIteration,
      &newer->lastUpdateIteration);

  if( misspec )
    newer->type = CL_Broken;

  else
    __specpriv_commit_io( &older->io_events, &commit_redux);

  heap_unmap( &partial_redux );
  heap_unmap( &commit_redux );

  return misspec;
}


// both older and newer are locked!
static Bool __specpriv_combine_checkpoints(Checkpoint *older, Checkpoint *newer)
{
  DEBUG(printf("Combining checkpoints (worker %u) for iterations %d and %d\n",
    __specpriv_my_worker_id(), older->iteration, newer->iteration));

  // Try to combine the partial private, redux heaps
  // into the newer copy.  Misspeculation may occur
  // during this operation.

  Bool misspec = __specpriv_combine_private(older,newer)
              || __specpriv_combine_redux(older,newer);


  DEBUG(printf("Done combining checkpoints (worker %u) for iterations %d and %d\n",
    __specpriv_my_worker_id(), older->iteration, newer->iteration));
  return misspec;
}

// Assumes that I own NO locks -- not the checkpoints, nor the manager
Bool __specpriv_commit_zero_or_more_checkpoints(CheckpointManager *mgr)
{
  DEBUG(printf("Worker %u begins committing checkpoints...\n", __specpriv_my_worker_id() ));

  for(;;)
  {
    // Do a quick test before locking to avoid
    // contention.  We will check again after
    // we have the lock...

    // Is alpha complete?
    Checkpoint *alpha = mgr->used.first;
    if( !alpha || alpha->type != CL_Complete )
      return 0;

    // How about the second checkpoint?
    Checkpoint *beta = alpha->next;
    if( !beta || beta->type != CL_Complete )
      return 0;

    acquire_lock( &alpha->lock );
    acquire_lock( &beta->lock );

    // ensure that my locking discipline isn't bullshit
    if( alpha->type != CL_Complete
    ||  alpha->next != beta
    ||  beta->type  != CL_Complete )
    {
      release_lock( &beta->lock );
      release_lock( &alpha->lock );
      return 0;
    }

    // Mark alpha as NOT complete;
    // This will temporarily stop contention
    // of other workers who are trying to
    // commit stuff...
    alpha->type = CL_Free;

    // First and next are both complete.
    Bool misspec = __specpriv_combine_checkpoints(alpha, beta);

    if( misspec )
    {
      beta->type = CL_Broken;

      release_lock( &beta->lock );
      release_lock( &alpha->lock );

      __specpriv_misspec_at(beta->iteration,
        "Misspeculation during checkpoint");
      return 1;
    }

    // Remove the second checkpoint from
    // the used list, put it in the free list.
    acquire_lock( &mgr->lock );

    assert( alpha == mgr->used.first );

    pop_front( &mgr->used );
    push_back( &mgr->free, alpha );

    release_lock( &mgr->lock );
    release_lock( &beta->lock );
    release_lock( &alpha->lock );

    // try to combine even more...
  }

  return 0;
}


static void __specpriv_worker_perform_checkpoint_locked(Checkpoint *chkpt)
{
  CheckpointRecord *rec = 0;
  (void) rec;
  TOUT(
    if( numCheckpoints < MAX_CHECKPOINTS )
      rec = &checkpoints[ numCheckpoints ];
  );

  MappedHeap partial_priv, partial_shadow, partial_redux;
  mapped_heap_init( &partial_priv );
  mapped_heap_init( &partial_shadow );
  mapped_heap_init( &partial_redux );

  heap_map_anywhere( &chkpt->heap_priv, &partial_priv );
  heap_map_anywhere( &chkpt->heap_shadow, &partial_shadow );
  heap_map_anywhere( &chkpt->heap_redux, &partial_redux );

  heap_alloc( &partial_redux, chkpt->redux_used );

  if( chkpt->num_workers == 0 )
    __specpriv_initialize_partial_checkpoint(chkpt, &partial_shadow, &partial_redux);

  // Commit /my/ private values to the partial heap
  TOUT( if(rec) TIME(rec->private_start); );
  {
    __specpriv_distill_worker_private_into_partial(chkpt, &partial_priv, &partial_shadow);
  }
  TOUT( if(rec) TIME(rec->private_stop); );

  // Commit /my/ reduction values to the partial heap
  TOUT( if(rec) TIME(rec->redux_start); );
  {
    __specpriv_distill_worker_redux_into_partial(&partial_redux,
                                                 &chkpt->lastUpdateIteration);
  }
  TOUT( if(rec) TIME(rec->redux_stop); );

  // Commit /my/ deferred IO to the partial heap.
  TOUT( if(rec) TIME(rec->io_start); );
  {
    __specpriv_copy_io_to_redux( &chkpt->io_events, &partial_redux);
  }
  TOUT( if(rec) TIME(rec->io_stop); );


  chkpt->redux_used = heap_used( &partial_redux );

  heap_unmap( &partial_redux );
  heap_unmap( &partial_shadow );
  heap_unmap( &partial_priv );

  ++chkpt->num_workers;
  if( __specpriv_num_workers() == chkpt->num_workers )
    chkpt->type = CL_Complete;

  rec = 0;
}

void __specpriv_worker_perform_checkpoint(int isFinalCheckpoint)
{
  uint64_t checkpoint_start, checkpoint_stop;
  TIME( checkpoint_start );

  assert( ! __specpriv_i_am_main_process() );

  Iteration currentIter = __specpriv_current_iter();
  if (!__specpriv_runOnEveryIter()) {
    const Wid numWorkers = __specpriv_num_workers();
    currentIter -= currentIter % numWorkers;
  }
  const Iteration effectiveIter = (isFinalCheckpoint) ? LAST_ITERATION : currentIter;

  ParallelControlBlock *pcb = __specpriv_get_pcb();
  if( pcb->misspeculation_happened && pcb->misspeculated_iteration <= currentIter )
    _exit(0);

  Checkpoint *chkpt = __specpriv_get_checkpoint_for_iter(&pcb->checkpoints, effectiveIter);
  if( !chkpt )
  {
    assert( pcb->misspeculation_happened && pcb->misspeculated_iteration <= currentIter );
    _exit(0);
  }

  acquire_lock( &chkpt->lock );
  {
    DEBUG(printf("Worker %u begins checkpointing at iteration %u\n",
      __specpriv_my_worker_id(), effectiveIter));

    __specpriv_worker_perform_checkpoint_locked(chkpt);

    DEBUG(printf("Worker %u ends checkpointing at iteration %u\n",
      __specpriv_my_worker_id(), effectiveIter));
  }
  release_lock( &chkpt->lock );

#if (WHO_DOES_CHECKPOINTS & SLOWEST_WORKER) != 0
  __specpriv_commit_zero_or_more_checkpoints( &pcb->checkpoints );
#endif

  TIME( checkpoint_stop );
  TOUT(
    if( numCheckpoints < MAX_CHECKPOINTS )
    {
      CheckpointRecord *rec = &checkpoints[ numCheckpoints ];
      rec->checkpoint_start = checkpoint_start;
      rec->checkpoint_stop = checkpoint_stop;
    }

    worker_time_in_checkpoints += (checkpoint_stop - checkpoint_start);
    ++numCheckpoints;
  );
}

void __specpriv_distill_checkpoints_into_liveout(CheckpointManager *mgr)
{
  assert( __specpriv_i_am_main_process() );

  __specpriv_commit_zero_or_more_checkpoints(mgr);

  // Adopt changes from Complete checkpoints
  while( !list_empty( &mgr->used ) )
  {
    Checkpoint *chkpt = mgr->used.first;
    if( chkpt->type != CL_Complete )
      break;
    pop_front( &mgr->used );

    DEBUG(printf(" * complete checkpoint %d\n", chkpt->iteration));

    MappedHeap commit_priv, commit_shadow, commit_redux;
    mapped_heap_init( &commit_priv );
    mapped_heap_init( &commit_shadow );
    mapped_heap_init( &commit_redux );

    heap_map_anywhere( &chkpt->heap_priv, &commit_priv );
    heap_map_anywhere( &chkpt->heap_shadow, &commit_shadow );
    heap_map_anywhere( &chkpt->heap_redux, &commit_redux );

    __specpriv_commit_io( &chkpt->io_events, &commit_redux );
    __specpriv_distill_committed_private_into_main( chkpt, &commit_priv, &commit_shadow );
    __specpriv_distill_committed_redux_into_main(&commit_redux,
                                                 chkpt->lastUpdateIteration);
    mgr->main_checkpoint->iteration = chkpt->iteration;

    heap_unmap( &commit_redux );
    heap_unmap( &commit_shadow );
    heap_unmap( &commit_priv );

    // Free this checkpoint.
    chkpt->type = CL_Free;
    push_back( &mgr->free, chkpt );
  }

  // All remaining checkpoints should be squashed.
  while( !list_empty( &mgr->used ) )
  {
    Checkpoint *squash = pop_front( &mgr->used );
    DEBUG(printf(" * squashing checkpoint %d\n", squash->iteration));
    squash->type = CL_Free;
    push_back( &mgr->free, squash );
  }
}





