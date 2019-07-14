#define DEBUG_TYPE "priv-remed"

#include "llvm/ADT/Statistic.h"

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"

#include "liberty/Analysis/FindSource.h"
#include "liberty/Orchestration/PrivRemed.h"
#include "liberty/Utilities/GepRange.h"
#include "liberty/Utilities/GlobalMalloc.h"
#include "liberty/Utilities/ReachabilityUtil.h"

// conservative privitization in many cases is as expensive as memory versioning
// and locality private. Need to always keep track of who wrote last.
//#define DEFAULT_PRIV_REMED_COST 1
#define DEFAULT_PRIV_REMED_COST 100
#define FULL_OVERLAP_PRIV_REMED_COST 70

namespace liberty {
using namespace llvm;

STATISTIC(numPrivNoMemDep,
          "Number of false mem deps removed by privitization");

void PrivRemedy::apply(Task *task) {
  this->task = task;
  replacePrivateLoadsStore((Instruction*)this->storeI);
}

bool PrivRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<PrivRemedy> privRhs =
      std::static_pointer_cast<PrivRemedy>(rhs);
  return this->storeI < privRhs->storeI;
}

// verify that noone from later iteration reads the written value by this store.
// conservatively ensure that the given store instruction is not part of any
// loop-carried memory flow (RAW) dependences
bool PrivRemediator::isPrivate(const Instruction *I) {
  auto pdgNode = pdg->fetchNode(const_cast<Instruction*>(I));
  for (auto edge : pdgNode->getOutgoingEdges()) {
    if (edge->isLoopCarriedDependence() && edge->isMemoryDependence() &&
        edge->isRAWDependence() && pdg->isInternal(edge->getIncomingT()))
      return false;
  }
  return true;
}

BasicBlock *getLoopEntryBB(const Loop *loop) {
  BasicBlock *header = loop->getHeader();
  BranchInst *term = dyn_cast<BranchInst>(header->getTerminator());
  BasicBlock *headerSingleInLoopSucc = nullptr;
  if (term) {
    for (unsigned sn = 0; sn < term->getNumSuccessors(); ++sn) {
      BasicBlock *succ = term->getSuccessor(sn);
      if (loop->contains(succ)) {
        if (headerSingleInLoopSucc) {
          headerSingleInLoopSucc = nullptr;
          break;
        } else
          headerSingleInLoopSucc = succ;
      }
    }
  }
  return headerSingleInLoopSucc;
}

bool isTransLoopInvariant(const Value *val, const Loop *L) {
  if (L->isLoopInvariant(val))
    return true;

  if (auto inst = dyn_cast<Instruction>(val)) {

    // limit to only arithmetic/logic ops
    if (!inst->isBinaryOp() && !inst->isCast() && !inst->isLogicalShift() &&
        !inst->isArithmeticShift() && !inst->isBitwiseLogicOp())
      return false;

    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
      if (!isTransLoopInvariant(inst->getOperand(i), L))
        return false;
    }
    return true;
  }
  return false;
}

bool extractValuesInSCEV(const SCEV *scev,
                         std::unordered_set<const Value *> &involvedVals,
                         ScalarEvolution *se) {
  if (!scev)
    return false;

  if (auto unknown = dyn_cast<SCEVUnknown>(scev)) {
    involvedVals.insert(unknown->getValue());
    return true;
  } else if (isa<SCEVConstant>(scev))
    return true;
  else if (auto *cast = dyn_cast<SCEVCastExpr>(scev))
    return extractValuesInSCEV(cast->getOperand(), involvedVals, se);
  else if (auto nary = dyn_cast<SCEVNAryExpr>(scev)) {
    for (unsigned i = 0; i < nary->getNumOperands(); ++i) {
      if (!extractValuesInSCEV(nary->getOperand(i), involvedVals, se))
        return false;
    }
    return true;
  } else if (auto udiv = dyn_cast<SCEVUDivExpr>(scev)) {
    if (!extractValuesInSCEV(udiv->getLHS(), involvedVals, se))
      return false;
    return extractValuesInSCEV(udiv->getRHS(), involvedVals, se);
  } else if (isa<SCEVCouldNotCompute>(scev))
    return false;
  else
    // if any other type of SCEV is introduced, conservatively return false
    return false;
}

bool isLoopInvariantSCEV(const SCEV *scev, const Loop *L, ScalarEvolution *se) {
  if (se->isLoopInvariant(scev, L))
    return true;
  std::unordered_set<const Value *> involvedVals;
  bool success = extractValuesInSCEV(scev, involvedVals, se);
  if (!success)
    return false;
  bool allLoopInvariant = true;
  for (auto val : involvedVals) {
    if (L->isLoopInvariant(val)) {
      continue;
    } else if (isTransLoopInvariant(val, L)) {
      continue;
    } else if (const GlobalValue *globalSrc = liberty::findGlobalSource(val)) {
      std::vector<const Instruction *> srcs;
      bool noCaptureGV = findNoCaptureGlobalSrcs(globalSrc, srcs);
      if (!noCaptureGV) {
        allLoopInvariant = false;
        break;
      }
      for (auto src : srcs) {
        if (L->contains(src)) {
          allLoopInvariant = false;
          break;
        }
      }
    } else
      allLoopInvariant = false;
    if (!allLoopInvariant)
      break;
  }
  return allLoopInvariant;
}

Remediator::RemedResp
PrivRemediator::memdep(const Instruction *A, const Instruction *B,
                       bool LoopCarried, DataDepType dataDepTy, const Loop *L) {

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<PrivRemedy> remedy =
      std::shared_ptr<PrivRemedy>(new PrivRemedy());
  remedy->cost = DEFAULT_PRIV_REMED_COST;

  bool WAW = dataDepTy == DataDepType::WAW;

  // need to be loop-carried WAW where the privitizable store is either A or B
  if (LoopCarried && WAW &&
      ((isa<StoreInst>(A) && isPrivate(A)) ||
       (isa<StoreInst>(B) && isPrivate(B)))) {
    ++numPrivNoMemDep;
    remedResp.depRes = DepResult::NoDep;
    if (isa<StoreInst>(A) && isPrivate(A))
      remedy->storeI = dyn_cast<StoreInst>(A);
    else
      remedy->storeI = dyn_cast<StoreInst>(B);

    remedy->type = PrivRemedy::Normal;
    remedResp.remedy = remedy;

    DEBUG(errs() << "PrivRemed removed mem dep between inst " << *A << "  and  "
                 << *B << '\n');

    // TODO: this restriction could be relaxed. Generalize beyond self-WAW
    if (A != B)
      return remedResp;

    const StoreInst *privStore = remedy->storeI;

    // evaluate if the private store overwrites the same memory locations and
    // executes the same number of times for every iteration of the loop of
    // interest. If full-overlap is proved for this write then assign
    // PrivRemedy::FullOverlap type to the remedy.

    // check if address of store is either a loop-invariant (to the loop of
    // interest), or a gep with only constant or affine SCEVAddRecExpr (to loop
    // with loop-invariant trip counts) indices
    //
    const Value *ptrPrivStore = privStore->getPointerOperand();
    const Loop* scevLoop = nullptr;
    if (!L->isLoopInvariant(ptrPrivStore) &&
        !isa<GetElementPtrInst>(ptrPrivStore))
      return remedResp;
    else if (!L->isLoopInvariant(ptrPrivStore)) {
      const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptrPrivStore);
      // traverse all the indices of the gep, make sure that they are all
      // constant or affine SCEVAddRecExpr (to loops with loop-invariant trip
      // counts, and with loop-invariant step and start)
      for (auto idx = gep->idx_begin(); idx != gep->idx_end(); ++idx) {
        const Value *idxV = *idx;
        if (L->isLoopInvariant(idxV))
          continue;
        else if (isTransLoopInvariant(idxV, L))
          continue;
        else if (se->isSCEVable(idxV->getType())) {
          const SCEVAddRecExpr *addRec =
              dyn_cast<SCEVAddRecExpr>(se->getSCEV(const_cast<Value *>(idxV)));
          if (!addRec)
            return remedResp;
          if (!addRec->isAffine())
            return remedResp;

          if (scevLoop && scevLoop != addRec->getLoop())
              return remedResp;
          scevLoop = addRec->getLoop();
          if (scevLoop == L || !L->contains(scevLoop))
            return remedResp;

          if (!se->hasLoopInvariantBackedgeTakenCount(scevLoop))
            return remedResp;

          if (!isLoopInvariantSCEV(addRec->getStart(), L, se) ||
              !isLoopInvariantSCEV(addRec->getStepRecurrence(*se), L, se))
            return remedResp;

        } else
          return remedResp;
      }
    }

    // ensure that on every iter of loop of interest the private store will
    // execute the same number of times.
    //
    // the most accurate approach would be to explore all the ctrl deps until
    // you reach the branch of the header of the loop of interest.
    //
    // for now we use a more conservative but a bit simpler approach.
    // check if the private store executes on every iter of the loop of interest
    // or in every iter of an inner loop which executes on every iter of outer
    // loop. This approach address only loop depth of 1 and does not take
    // advantage of loop-invariant if-statements; still in practise is very
    // common. Note also that this approach requires that all involved BBs are
    // in the same function (postdominator is intraprocedural)

    const BasicBlock *loopEntryBB = getLoopEntryBB(L);
    if (!loopEntryBB)
      return remedResp;

    if (loopEntryBB->getParent() != privStore->getFunction())
      return remedResp;
    if(pdt->dominates(privStore->getParent(), loopEntryBB)) {
      // private store executes on every iter of loop of interest
      remedy->type = PrivRemedy::FullOverlap;
      remedResp.remedy = remedy;
      return remedResp;
    }

    // private store does not postdominate loop of interest entry BB.
    // check if private store executes on every iter of inner loop that runs for
    // loopInvariant iterations and the header of inner loop executes on every
    // iter of loop of interest
    const Loop *innerLoop;
    innerLoop = li->getLoopFor(privStore->getParent());
    if (!innerLoop)
      return remedResp;
    if (innerLoop->getHeader()->getParent() != loopEntryBB->getParent())
      return remedResp;
    if (!pdt->dominates(innerLoop->getHeader(), loopEntryBB))
      return remedResp;
    const BasicBlock *innerLoopEntryBB = getLoopEntryBB(innerLoop);
    if (!innerLoopEntryBB)
      return remedResp;
    if (!pdt->dominates(privStore->getParent(), innerLoopEntryBB))
      return remedResp;
    if (!se->hasLoopInvariantBackedgeTakenCount(innerLoop))
      return remedResp;

    // success. private store executes same number of times on every loop of
    // interest iter
    remedy->type = PrivRemedy::FullOverlap;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
