#ifndef LIBERTY_PUREIO_COMMIT_H
#define LIBERTY_PUREIO_COMMIT_H

#include "types.h"

//------------------------------------------------------------------------
// Methods for progressive commit.

void close_tx(TX *tx, uint32_t n, int blocking);
Scalar issue(SusOp *sop, int blocking);

int dispatch_event(TX *tx, Event *evt);

#if USE_COMMIT_THREAD
void *commit_thread(void *arg);
#endif


#endif

