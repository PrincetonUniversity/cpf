#define DEBUG_TYPE "doall-transform"

#include "liberty/CodeGen/DOALLTransform.h"

namespace liberty {
using namespace llvm;

void DOALLTransform::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<Selector>();
}

// DOALLTransform::linkParallelizedLoopToOriginalFunction is currently a copy of
// Parallelization::linkParallelizedLoopToOriginalFunction from noelle (Matni,
// Campanoni)
void DOALLTransform::linkParallelizedLoopToOriginalFunction(
    Module *module, BasicBlock *originalPreHeader,
    BasicBlock *startOfParLoopInOriginalFunc,
    BasicBlock *endOfParLoopInOriginalFunc, Value *envArray,
    Value *envIndexForExitVariable,
    SmallVector<BasicBlock *, 10> &loopExitBlocks) {

  /*
   * Create the global variable for the parallelized loop.
   */
  auto globalBool = new GlobalVariable(*module, int32, /*isConstant=*/false,
                                       GlobalValue::ExternalLinkage,
                                       Constant::getNullValue(int32));
  auto const0 = ConstantInt::get(int32, 0);
  auto const1 = ConstantInt::get(int32, 1);

  /*
   * Fetch the terminator of the preheader.
   */
  auto originalTerminator = originalPreHeader->getTerminator();

  /*
   * Fetch the header of the original loop.
   */
  auto originalHeader = originalTerminator->getSuccessor(0);

  /*
   * Check if another invocation of the loop is running in parallel.
   */
  IRBuilder<> loopSwitchBuilder(originalTerminator);
  auto globalLoad = loopSwitchBuilder.CreateLoad(globalBool);
  auto compareInstruction = loopSwitchBuilder.CreateICmpEQ(globalLoad, const0);
  loopSwitchBuilder.CreateCondBr(compareInstruction,
                                 startOfParLoopInOriginalFunc, originalHeader);
  originalTerminator->eraseFromParent();

  IRBuilder<> endBuilder(endOfParLoopInOriginalFunc);

  /*
   * Load exit block environment variable and branch to the correct loop exit
   * block
   */
  if (loopExitBlocks.size() == 1) {
    endBuilder.CreateBr(loopExitBlocks[0]);
  } else {
    auto exitEnvPtr = endBuilder.CreateInBoundsGEP(
        envArray,
        ArrayRef<Value *>({cast<Value>(ConstantInt::get(int64, 0)),
                           endBuilder.CreateMul(envIndexForExitVariable,
                                                ConstantInt::get(int64, 8))}));
    auto exitEnvCast = endBuilder.CreateIntCast(
        endBuilder.CreateLoad(exitEnvPtr), int32, /*isSigned=*/false);
    auto exitSwitch = endBuilder.CreateSwitch(exitEnvCast, loopExitBlocks[0]);
    for (int i = 1; i < loopExitBlocks.size(); ++i) {
      exitSwitch->addCase(ConstantInt::get(int32, i), loopExitBlocks[i]);
    }
  }

  /*
   * NOTE(angelo): LCSSA constants need to be replicated for parallelized code
   * path
   */
  for (auto bb : loopExitBlocks) {
    for (auto &I : *bb) {
      if (auto phi = dyn_cast<PHINode>(&I)) {
        auto bbIndex = phi->getBasicBlockIndex(originalHeader);
        if (bbIndex == -1) {
          continue;
        }
        auto val = phi->getIncomingValue(bbIndex);
        if (isa<Constant>(val)) {
          phi->addIncoming(val, endOfParLoopInOriginalFunc);
        }
        continue;
      }
      break;
    }
  }

  /*
   * Set/Reset global variable so only one invocation of the loop is run in
   * parallel at a time.
   */
  if (startOfParLoopInOriginalFunc == endOfParLoopInOriginalFunc) {
    endBuilder.SetInsertPoint(&*endOfParLoopInOriginalFunc->begin());
    endBuilder.CreateStore(const1, globalBool);
  } else {
    IRBuilder<> startBuilder(&*startOfParLoopInOriginalFunc->begin());
    startBuilder.CreateStore(const1, globalBool);
  }
  endBuilder.SetInsertPoint(endOfParLoopInOriginalFunc->getTerminator());
  endBuilder.CreateStore(const0, globalBool);

  return;
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
  linkParallelizedLoopToOriginalFunction(
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

  int1 = IntegerType::get(M.getContext(), 1);
  int8 = IntegerType::get(M.getContext(), 8);
  int16 = IntegerType::get(M.getContext(), 16);
  int32 = IntegerType::get(M.getContext(), 32);
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
