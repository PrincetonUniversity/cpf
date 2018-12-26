#include "pcb.h"
#include "fiveheaps.h"
#include "checkpoint.h"

static ParallelControlBlock * the_pcb = 0;

static void __specpriv_init_pcb(void)
{
  the_pcb = (ParallelControlBlock*) __specpriv_alloc_meta( sizeof(ParallelControlBlock) );

  __specpriv_init_checkpoint_manager( &the_pcb->checkpoints );
}

ParallelControlBlock *__specpriv_get_pcb(void)
{
  if( !the_pcb )
    __specpriv_init_pcb();

  return the_pcb;
}

void __specpriv_destroy_pcb(void)
{
  __specpriv_destroy_checkpoint_manager( &the_pcb->checkpoints );
  __specpriv_free_meta( the_pcb );
  the_pcb = 0;
}
