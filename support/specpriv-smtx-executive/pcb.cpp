#include "internals/pcb.h"

#include <sys/mman.h>

namespace specpriv_smtx
{

static PCB* the_pcb = 0;

static void init_pcb(void)
{
  the_pcb = (PCB*)mmap(0, sizeof(PCB), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

PCB* get_pcb(void)
{
  if( !the_pcb )
    init_pcb();

  return the_pcb;
}

void destroy_pcb(void)
{
  if ( the_pcb )
    munmap(the_pcb, sizeof(PCB));
  the_pcb = 0;
}

}
