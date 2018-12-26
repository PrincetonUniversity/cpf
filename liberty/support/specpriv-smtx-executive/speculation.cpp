#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "api.h"
#include "internals/constants.h"
#include "internals/debug.h"
#include "internals/pcb.h"
#include "internals/private.h"
#include "internals/smtx/communicate.h"

namespace specpriv_smtx
{

void PREFIX(predict)(uint64_t observed, uint64_t expected)
{
  if( observed != expected )
    PREFIX(misspec)("Value prediction failed");
}

Iteration PREFIX(last_committed)(void)
{
  return get_pcb()->last_committed_iteration;
}

Iteration PREFIX(misspec_iter)(void)
{
  return get_pcb()->misspeculated_iteration;
}

void PREFIX(misspec)(const char* msg)
{
  Iteration iter = PREFIX(current_iter)();
  Wid       wid = PREFIX(my_worker_id)();

  PCB *pcb = get_pcb();

  pcb->misspeculation_happened = 1;
  pcb->misspeculated_worker = wid;

  if ( pcb->misspeculated_iteration == -1 || pcb->misspeculated_iteration > iter )
    pcb->misspeculated_iteration = iter;
  pcb->misspeculation_reason = msg;

  DBG("__specpriv_misspec: wid: %u, pid: %u, iter: %d, %s\n", wid, getpid(), iter, msg);

  // notify that misspeculation have happened to following stages
  broadcast_event( wid, (void*)0xDEADBEEF, 0, NULL, WRITE, MISSPEC );

  clear_incoming_queues( wid );

  if ( wid != MAIN_PROCESS_WID )
    PREFIX(worker_finishes)(0); // TODO: confirm the exit code -> is zero right here?
  else
    assert( false && "Main thread misspeculated!\n" );
}

void PREFIX(recovery_finished)(Exit e)
{
  PCB* pcb = get_pcb();

  Iteration mi = pcb->misspeculated_iteration;

  pcb->misspeculation_happened = 0;
  pcb->misspeculated_worker = 0;
  pcb->misspeculated_iteration = -1;
  pcb->misspeculation_reason = 0;
  pcb->last_committed_iteration = -1;

  if( e > 0 )
    pcb->exit_taken = e;

  set_current_iter(mi + 1);
}

static uint32_t callingcontext = 0;

uint32_t invokedepth = 0;

void PREFIX(loop_invocation)()
{
  invokedepth++;
}

void PREFIX(loop_exit)()
{
  invokedepth--;
}

void PREFIX(push_context)(const uint32_t ctxt)
{
#if CTXTDBG
  DBG("push_context: %u depth: %u\n", ctxt, invokedepth);
#endif
  if (invokedepth > 1) return;
  callingcontext = ctxt;
}

void PREFIX(pop_context)()
{
  if (invokedepth > 1) return;
  callingcontext = 0;
}

uint64_t PREFIX(get_context)()
{
#if CTXTDBG
  DBG("get_context: %u\n", callingcontext);
#endif
  return (uint64_t)callingcontext;
}

}
