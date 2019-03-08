#ifndef US_RECOVERY_H
#define US_RECOVERY_H

#include "version.h"

bool ver_spawn(ver_runnable fcn, ver_tid tid, uint64_t arg);
void ver_respawn(ver_runnable fcn, ver_tid tid, uint64_t arg);

int ver_wait(ver_tid tid);

void ver_recover(const ver_tid id);

void ver_recoverChild(const ver_tid id);

ver_tid ver_newTID(channel chan, uint32_t curr);

int ver_deleteTID(ver_tid id);

#endif /* US_RECOVERY_H */
