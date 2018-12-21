#include "types.h"
#include "constants.h"

namespace specpriv_smtx
{

void init_worker(Iteration current_iter, Wid wid);
Wid  get_pstage_replica_id(void);
void reset_current_iter(void);
void advance_iter(void);
void set_current_iter(Iteration iter);

}
