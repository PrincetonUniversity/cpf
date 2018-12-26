#include "internals/constants.h"

namespace specpriv_smtx
{

void register_handler();
void register_base_handler();
void reset_protection(Wid wid);
void set_page_rw(void* page);
}
