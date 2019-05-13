#ifndef LLVM_LIBERTY_SPEC_PRIV_API_H
#define LLVM_LIBERTY_SPEC_PRIV_API_H

#include <stdint.h>
#include "types.h"

Wid __specpriv_num_workers(void);
Wid __specpriv_my_worker_id(void);

Bool __specpriv_i_am_main_process(void);

void __specpriv_misspec(const char *);
void __specpriv_misspec_at(Iteration, const char *);

Iteration __specpriv_current_iter(void);

Iteration __specpriv_last_committed(void);
Iteration __specpriv_misspec_iter(void);

void __specpriv_recovery_done(Exit);

Bool __specpriv_runOnEveryIter(void);

#endif

