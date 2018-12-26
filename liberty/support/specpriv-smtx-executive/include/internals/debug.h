#include <stdlib.h>

#include "constants.h"
#include "types.h"

namespace specpriv_smtx
{

void   init_debug(unsigned num_all_workers);
size_t DBG(const char* format, ...);
size_t PROFDUMP(const char* format, ...);

}
