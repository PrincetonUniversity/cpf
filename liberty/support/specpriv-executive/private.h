#ifndef LLVM_LIBERTY_SPEC_PRIV_PRIVATE_H
#define LLVM_LIBERTY_SPEC_PRIV_PRIVATE_H

#include "types.h"
#include "heap.h"
#include "checkpoint.h"

void __specpriv_init_private(void);

// Set first iteration number
void __specpriv_set_first_iter(Iteration);

// Get first iteration number
Iteration __specpriv_get_first_iter(void);

// Set the current iteration number, and
// possibly perform a checkpoint.
void __specpriv_advance_iter(Iteration, uint32_t);


// partial <-- later(worker,partial)
// where worker, partial are from the same checkpoint-group of iterations.
// return true if misspeculation is detected during this operation.
Bool __specpriv_distill_worker_private_into_partial(Checkpoint *partial, MappedHeap *partial_priv, MappedHeap *partial_shadow);


// partial <-- later(committed,partial)
// where committed comes from an EARLIER checkpoint-group of iterations.
// return true if misspeculation is detected during this operation.
Bool __specpriv_distill_committed_private_into_partial(Checkpoint *commit, MappedHeap *commit_priv, MappedHeap *commit_shadow, Checkpoint *partial, MappedHeap *partial_priv, MappedHeap *partial_shadow);

// main <-- later(main,committed)
// where main comes from an EARLIER checkpoint-group of iterations.
// return true if misspeculation is detected during this operation.
Bool __specpriv_distill_committed_private_into_main(Checkpoint *commit, MappedHeap *commit_priv, MappedHeap *commit_shadow);

#endif

