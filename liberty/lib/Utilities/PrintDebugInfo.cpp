#include "liberty/Utilities/PrintDebugInfo.h"

#include "llvm/IR/Instruction.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void liberty::printInstDebugInfo(Instruction *I) {
  const DebugLoc &debugLoc = I->getDebugLoc();
  if (debugLoc) {
    DIScope *scope = dyn_cast<DIScope>(debugLoc->getScope());
    if (scope) {
      std::string filename = scope->getFilename();
      errs() << " (filename:" << filename << ", line:";
    } else
      errs() << " (line:";

    errs() << debugLoc.getLine() << ", col:" << debugLoc.getCol() << ")";
  }
}
