#define DEBUG_TYPE "doall-transform"

#include "liberty/CodeGen/DOALLTransform.h"

namespace liberty {
using namespace llvm;

void DOALLTransform::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<Selector>();
  //au.addRequired<Classify>();
  au.addRequired<ReadPass>();
  au.addRequired<ModuleLoops>();
}

void DOALLTransform::reallocateEnvAsShared(Value *alloc, Ctx *fcn_ctx) {
  Ptrs aus;
  assert(read->getUnderlyingAUs(alloc, fcn_ctx, aus) &&
         "Failed to create AU objects for the env array object?!");

  HeapAssignment::AUSet &shared = asgn->getSharedAUs();
  for (Ptrs::iterator i = aus.begin(), e = aus.end(); i != e; ++i) {
    shared.insert(i->au);
  }
}

bool DOALLTransform::doallParallelizeLoop(LoopDependenceInfo *LDI,
                                          SelectedRemedies *selectedRemeds) {

  DEBUG(errs() << "Parallelizer:  Try to parallelize the loop \""
               << LDI->header->getName()
               << "::" << LDI->header->getParent()->getName() << "\"\n");

  bool modified = false;

  read = &getAnalysis<ReadPass>().getProfileInfo();
  //Classify &classify = getAnalysis<Classify>();
  //asgn = &classify.getAssignmentFor(LDI->loop);
  std::set<const Value*> alreadyInstrumented;

  std::vector<Type *> formals;
  fv2v = FunctionType::get(voidty, formals, false);

  // check if selected remedies are yet implemented
  bool localityRemedUsed = false;
  bool memVerUsed = false;
  for (auto &remed : *selectedRemeds) {
    if (!remed->getRemedyName().equals("counted-iv-remedy") &&
        !remed->getRemedyName().equals("locality-remedy") &&
        //!remed->getRemedyName().equals("mem-ver-remedy") &&
        !remed->getRemedyName().equals("priv-remedy") &&
        !remed->getRemedyName().equals("redux-remedy") &&
        !remed->getRemedyName().equals("txio-remedy")) {
      DEBUG(errs()
            << "At least one selected remedy is not implemented fully yet\n");
      return false;
    }

    if (remed->getRemedyName().equals("locality-remedy")) {
      localityRemedUsed = true;
      memVerUsed = true;
      customHeapAlloc = true;
      nonSpecPrivRedux = false;
    }

    remed->read = read;
    remed->asgn = asgn;
    remed->alreadyInstrumented = &alreadyInstrumented;
    remed->u8 = int8;
    remed->u32 = int32;
    remed->voidptr = voidptr;
    remed->voidty = voidty;
    remed->mod = mod;

    nonSpecPrivRedux |=
        (remed->getRemedyName().equals("priv-remedy") & !localityRemedUsed);

    memVerUsed |= remed->getRemedyName().equals("mem-ver-remedy");
  }

  // set worker count
  //LDI->maximumNumberOfCoresForTheParallelization = 10;
  LDI->maximumNumberOfCoresForTheParallelization = 24;
  // set chunk size
  //LDI->DOALLChunkSize = 5000;
  LDI->DOALLChunkSize = 4;

  // doall should be applicable now

  if (localityRemedUsed || nonSpecPrivRedux || doall->canBeAppliedToLoop(LDI)) {
    /*
     * Apply DOALL.
     */
    DEBUG(errs() << "DOALL is applicable\n");
    doall->reset();
    modified = doall->apply(LDI);
    //modified = doall->apply(LDI, memVerUsed);
  } else
    DEBUG(errs() << "DOALL is not applicable\n");

  assert(modified);

  task = (DOALLTask *)doall->getTasks()[0];

  if (memVerUsed || nonSpecPrivRedux) {
    //LiveoutStructure liveouts;
    //modified |= demoteLiveOutsAndPhis(LDI->getLoop(), liveouts);
    Ctx *fcn_ctx = read->getCtx(LDI->function);

    reallocateEnvAsShared(doall->getEnvArray(), fcn_ctx);

    auto &reduxArrAllocs = doall->getReduxArrAllocs();
    for (auto &alloc : reduxArrAllocs)
      reallocateEnvAsShared(alloc, fcn_ctx);
  }

  // apply all selected remedies
  for (auto &remed: *selectedRemeds) {
    if (remed->getRemedyName().equals("counted-iv-remedy") ||
      //  remed->getRemedyName().equals("mem-ver-remedy") ||
        remed->getRemedyName().equals("redux-remedy") ||
        remed->getRemedyName().equals("txio-remedy"))
      // already fixed or fixed implicitly
      continue;

    if (remed->getRemedyName().equals("locality-remedy")) {
      //modified |= remed->apply(LDI);
      remed->apply(task);
      modified = true;
    } else if (remed->getRemedyName().equals("priv-remedy")) {
      if (!nonSpecPrivRedux)
        continue;

      PrivRemedy *privRemed = (PrivRemedy *)&*remed;
      const Value *ptr = privRemed->storeI->getPointerOperand();

      Ctx *fcn_ctx = read->getCtx(LDI->function);
      Ptrs aus;
      assert(read->getUnderlyingAUs(ptr, fcn_ctx, aus) &&
             "Failed to create AU objects for non spec priv?!");

      for (Ptrs::iterator i = aus.begin(), e = aus.end(); i != e; ++i)
        nonSpecPrivAUs.insert(i->au);

      remed->apply(task);
      modified = true;
    }

    assert("Not fully implemented remedy");
  }

  // it seems better to perfom adjustForSpec and manageHeaps at the end after
  // all the noelle transformations
  /*
  if (localityRemedUsed) {
    //modified |= manageHeaps();
    adjustForSpec();
  }
  */

  /*
   * Check if the loop has been parallelized.
   */
  if (!modified) {
    return false;
  }

  /*
   * Fetch the environment array where the exit block ID has been stored.
   */
  auto envArray = doall->getEnvArray();
  assert(envArray != nullptr);

  /*
   * Fetch entry and exit point executed by the parallelized loop.
   */
  auto entryPoint = doall->getParLoopEntryPoint();
  auto exitPoint = doall->getParLoopExitPoint();
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
   * Manage heaps for locality remediator
  */
  if (localityRemedUsed || nonSpecPrivRedux) {
    //manageHeaps();
    adjustForSpecDOALL(LDI);
  }

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

  selector = &getAnalysis<Selector>();
  asgn = &selector->getAssignment();
  customHeapAlloc = false;
  nonSpecPrivRedux = false;

  mod = &M;
  int8 = IntegerType::get(M.getContext(), 8);
  int16 = IntegerType::get(M.getContext(), 16);
  int32 = IntegerType::get(M.getContext(), 32);
  int64 = IntegerType::get(M.getContext(), 64);
  int64 = IntegerType::get(M.getContext(), 64);
  voidty = Type::getVoidTy(M.getContext());
  voidptr = PointerType::getUnqual(int8);

  //DOALL doall{M, static_cast<Verbosity>(2)};
  doall = std::make_unique<DOALL>(M, static_cast<Verbosity>(2));

  // parallelize loops
  for (auto i = selector->sloops_begin(), e = selector->sloops_end(); i != e;
       ++i) {
    BasicBlock *loopHeader = *i;
    auto &loop2DepInfo = selector->getLoop2DepInfo();
    LoopDependenceInfo *loopDepInfo = loop2DepInfo[loopHeader].get();
    auto &loop2SelectedRemedies = selector->getLoop2SelectedRemedies();
    SelectedRemedies *selectedRemeds = loop2SelectedRemedies[loopHeader].get();
    //modified |= doallParallelizeLoop(loopDepInfo, selectedRemeds, doall);
    modified |= doallParallelizeLoop(loopDepInfo, selectedRemeds);
  }
  if (customHeapAlloc) {
    manageHeaps();
  }
  else if (nonSpecPrivRedux) {
    manageNonSpecHeaps();
  }
  return modified;
}

char DOALLTransform::ID = 0;
static RegisterPass<DOALLTransform> x("doall-transform",
                                      "Perform DOALL transform");

} // namespace liberty
