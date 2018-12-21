#include <sys/types.h>

#include "internals/types.h"

namespace specpriv_smtx
{

void process_reverse_commit_queue(Wid wid);
void process_incoming_packets(Wid wid, Iteration iter);
void try_commit();
void commit(uint32_t n_all_workers, pid_t* worker_pids);

}
