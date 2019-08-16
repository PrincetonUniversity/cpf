#ifndef LLVM_LIBERTY_SPEC_PRIV_API_H
#define LLVM_LIBERTY_SPEC_PRIV_API_H

#include <stdint.h>
#include <pthread.h>
#include "types.h"

Wid __specpriv_num_workers(void);
Wid __specpriv_my_worker_id(void);
Bool __specpriv_is_on_iter(void);

Bool __specpriv_i_am_main_process(void);

void __specpriv_misspec(const char *);
void __specpriv_misspec_at(Iteration, const char *);

Iteration __specpriv_current_iter(void);

Iteration __specpriv_last_committed(void);
Iteration __specpriv_misspec_iter(void);

void __specpriv_recovery_done(Exit);

Bool __specpriv_runOnEveryIter(void);

uint32_t __specpriv_get_ckpt_check(void);

Iteration __specpriv_last_redux_update_iter(void);

pthread_t *__specpriv_ckpt_thread(void);
Bool __specpriv_pthread_to_join(void);

pid_t *__specpriv_ckpt_pid(void);
Bool __specpriv_pid_to_join(void);

Iteration __specpriv_ckpt_cur_iter(void);
void __specpriv_set_ckpt_cur_iter(Iteration);

pthread_mutex_t *__specpriv_thread_lock(void);
pthread_cond_t *__specpriv_ckpt_thread_cond(void);

#endif

