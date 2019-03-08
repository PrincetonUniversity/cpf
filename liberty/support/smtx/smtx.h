#ifndef SMTX_H
#define SMTX_H

#include "us_recovery.h"

void ver_doRecovery(const ver_tid tid);
void ver_tryCommitStage(const ver_tid tid, uint64_t arg);
void ver_commitStage(const ver_tid *tids, 
                     ver_runnable recover, 
                     ver_runnable commit,
                     uint64_t arg);

#endif /* SMTX_H */
