#ifndef LLVM_LIBERTY_SUPPORT_NOSPEC_EXECUTIVE_RUNTIME_H
#define LLVM_LIBERTY_SUPPORT_NOSPEC_EXECUTIVE_RUNTIME_H

#include "types.h"

#define MAX_WORKERS 32

typedef void*(*vfnptrv)(void *);

struct __specpriv_queue;

Wid __specpriv_num_available_workers(void);
uint32_t __specpriv_begin_invocation(void);
uint32_t __specpriv_end_invocation(void);
uint32_t __specpriv_spawn_workers(Iteration iterationNumber,
    vfnptrv startFun, void *arg, int stageNum);
uint32_t __specpriv_join_children(void);

uint32_t __specpriv_worker_finishes(Exit exitNumber);

struct __specpriv_queue * __specpriv_create_queue(void);
void __specpriv_reset_queue(struct __specpriv_queue *q);
void __specpriv_free_queue(struct __specpriv_queue *q);

void __specpriv_produce(struct __specpriv_queue *q, uint64_t v);
uint64_t __specpriv_consume(struct __specpriv_queue *q);

void __specpriv_begin_iter(void);
void __specpriv_end_iter(void);

Iteration __specpriv_misspec_iter(void);
Iteration __specpriv_current_iter(void);
Iteration __specpriv_last_committed(void);
void __specpriv_recovery_finished(Exit exitNumber);

#endif

