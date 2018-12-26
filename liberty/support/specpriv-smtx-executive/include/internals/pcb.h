#include "internals/constants.h"

#define PAD(suffix, size) char padding ## suffix [ALIGNMENT-size]

namespace specpriv_smtx
{

// Shared state for the parallel region (to be allocated in shared heap)
struct s_parallel_control_block
{
  // If the workers have finished, which
  // exit did the first-to-exit take?
  Exit                exit_taken;

  // Has misspeculation occurred?
  // If so, who, when, and why?
  Bool                misspeculation_happened;
  Wid                 misspeculated_worker;
  Iteration           misspeculated_iteration;
  const char *        misspeculation_reason;
  Iteration           last_committed_iteration;

#if SIMULATE_MISSPEC != 0
  Iteration           totalIterations;
  unsigned            numMisspecIterations;
  PAD(1, sizeof(Exit)+sizeof(Bool)+sizeof(Wid)+sizeof(Iteration)*2+sizeof(const char*)+sizeof(unsigned));
#else
  PAD(1, sizeof(Exit)+sizeof(Bool)+sizeof(Wid)+sizeof(Iteration)+sizeof(const char*));
#endif
};

typedef struct s_parallel_control_block PCB;

PCB* get_pcb(void);
void destroy_pcb(void);

}

#undef PAD
