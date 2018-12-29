#include "llvm/IR/User.h"

#include "liberty/Utilities/FindAllTransUses.h"

using namespace llvm;

void liberty::findAllTransUses(const Value *V, DenseSet<const Value *> &uses) {

  if(!uses.insert(V).second)
    return;

  typedef Value::const_user_iterator UseIt;
  for(UseIt use = V->user_begin(); use != V->user_end(); ++use) {
    findAllTransUses(*use, uses);
  }
}
