#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

namespace liberty {

  using namespace llvm;

  // Some common cases of GEP

  // Compute a GEP statically
  Constant *getGEP(Constant *structure, unsigned i);

  // Compute a GEP dynamically
  Instruction *getGEPInst(Value *structure, unsigned i);

  // Compute a GEP statically
  Constant *getGEP(Constant *structure, unsigned i, unsigned j);

  // Compute a GEP dynamically
  Instruction *getGEPInst(Value *structure, Value *i);

  // Compute a GEP dynamically
  Instruction *getGEPInst(Value *structure, Value *i, unsigned j);

  // Compute a GEP dynamically
  Instruction *getGEPInst(Value *structure, unsigned i, unsigned j);
}
