#define DEBUG_TYPE "doall-transform"

#include "liberty/CodeGen/DOALLTransform.h"

namespace liberty {
using namespace llvm;

void DOALLTransform::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<Selector>();
}

bool DOALLTransform::doallParallelizeLoop(LoopDependenceInfo *LDI,
                                          DOALL &doall) {

  DEBUG(errs() << "Parallelizer:  Try to parallelize the loop \""
               << LDI->header->getName()
               << "::" << LDI->header->getParent()->getName() << "\"\n");

  bool modified = false;
  if (doall.canBeAppliedToLoop(LDI)) {
    /*
     * Apply DOALL.
     */
    DEBUG(errs() << "DOALL is applicable\n");
    doall.reset();
    modified = doall.apply(LDI);
  } else
    DEBUG(errs() << "DOALL is not applicable\n");

  /*
   * Check if the loop has been parallelized.
   */
  if (!modified) {
    return false;
  }

  /*
   * Fetch the environment array where the exit block ID has been stored.
   */
  auto envArray = doall.getEnvArray();
  assert(envArray != nullptr);

  /*
   * Fetch entry and exit point executed by the parallelized loop.
   */
  auto entryPoint = doall.getParLoopEntryPoint();
  auto exitPoint = doall.getParLoopExitPoint();
  assert(entryPoint != nullptr && exitPoint != nullptr);

  /*
   * The loop has been parallelized.
   *
   * Link the parallelized loop within the original function that includes the
   * sequential loop.
   */
  DEBUG(errs() << "Parallelizer:  Link the parallelize loop\n");
  auto exitIndex = cast<Value>(
      ConstantInt::get(int64, LDI->environment->indexOfExitBlock()));
  Parallelization::linkParallelizedLoopToOriginalFunction(
      LDI->function->getParent(), LDI->preHeader, entryPoint, exitPoint,
      envArray, exitIndex, LDI->loopExitBlocks);

  /*
   * Return
   */
  DEBUG(errs() << "Parallelizer: Exit\n");
  return true;
}

bool DOALLTransform::runOnModule(Module &M) {
  DEBUG(errs() << "#################################################\n"
               << " DOALLTransform\n\n\n");
  bool modified = false;

  Selector &selector = getAnalysis<Selector>();

  int64 = IntegerType::get(M.getContext(), 64);

  DOALL doall{M, static_cast<Verbosity>(2)};

  // parallelize loops
  for (auto i = selector.sloops_begin(), e = selector.sloops_end(); i != e;
       ++i) {
    BasicBlock *loopHeader = *i;
    auto &loop2DepInfo = selector.getLoop2DepInfo();
    LoopDependenceInfo *loopDepInfo = loop2DepInfo[loopHeader].get();
    modified |= doallParallelizeLoop(loopDepInfo, doall);
  }
  return modified;
}

char DOALLTransform::ID = 0;
static RegisterPass<DOALLTransform> x("doall-transform",
                                      "Perform DOALL transform");

} // namespace liberty
