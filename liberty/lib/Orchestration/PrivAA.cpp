#define DEBUG_TYPE "priv-aa"

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Analysis/FindSource.h"
#include "liberty/Orchestration/PrivAA.h"
#include "liberty/Orchestration/PrivRemed.h"
#include "liberty/Utilities/GepRange.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/GlobalMalloc.h"
#include "liberty/Utilities/ReachabilityUtil.h"

#ifndef DEFAULT_PRIV_REMED_COST
#define DEFAULT_PRIV_REMED_COST 100
#endif

#ifndef FULL_OVERLAP_PRIV_REMED_COST
#define FULL_OVERLAP_PRIV_REMED_COST 70
#endif

namespace liberty {
using namespace llvm;

STATISTIC(numPrivNoMemDep, "Number of false mem deps removed by privitization");

BasicBlock *PrivAA::getLoopEntryBB(const Loop *loop) {
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

bool PrivAA::isTransLoopInvariant(const Value *val, const Loop *L) {
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

bool PrivAA::isLoopInvariantValue(const Value *V, const Loop *L) {
  if (L->isLoopInvariant(V)) {
    return true;
  } else if (isTransLoopInvariant(V, L)) {
    return true;
  } else if (const GlobalValue *globalSrc = liberty::findGlobalSource(V)) {
    return isLoopInvariantGlobal(globalSrc, L);
  } else
    return false;
}

bool PrivAA::extractValuesInSCEV(
    const SCEV *scev, std::unordered_set<const Value *> &involvedVals,
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

bool PrivAA::isLoopInvariantSCEV(const SCEV *scev, const Loop *L,
                                 ScalarEvolution *se) {
  if (se->isLoopInvariant(scev, L))
    return true;
  std::unordered_set<const Value *> involvedVals;
  bool success = extractValuesInSCEV(scev, involvedVals, se);
  if (!success)
    return false;
  bool allLoopInvariant = true;
  for (auto val : involvedVals) {
    allLoopInvariant &= isLoopInvariantValue(val, L);
    if (!allLoopInvariant)
      break;
  }
  return allLoopInvariant;
}

bool PrivAA::isCheapPrivate(const Instruction *I, const Value *ptr,
                            const Loop *L, Remedies &R) {

  if (I)
    ptr = liberty::getMemOper(I);
  if (!ptr)
    return false;
  if (!isa<PointerType>(ptr->getType()))
    return false;

  // const Ctx *ctx = read.getCtx(L);
  const HeapAssignment::AUToRemeds &cheapPrivs = asgn.getCheapPrivAUs();
  Ptrs aus;
  if (read.getUnderlyingAUs(ptr, ctx, aus)) {
    if (HeapAssignment::subOfAUSet(aus, cheapPrivs)) {
      R = asgn.getRemedForPrivAUs(aus);
      return true;
    }
  }
  return false;
}

unsigned long getRemediesCost(Remedies &R) {
  unsigned long totalCost = 0;
  for (auto remed : R) {
    totalCost += remed->cost;
  }
  return totalCost;
}

LoopAA::AliasResult PrivAA::alias(const Value *P1, unsigned S1,
                                  TemporalRelation rel, const Value *P2,
                                  unsigned S2, const Loop *L, Remedies &R) {

  if (rel == LoopAA::Same)
    return LoopAA::alias(P1, S1, rel, P2, S2, L, R);

  if (!isa<PointerType>(P1->getType()))
    return LoopAA::alias(P1, S1, rel, P2, S2, L, R);
  if (!isa<PointerType>(P2->getType()))
    return LoopAA::alias(P1, S1, rel, P2, S2, L, R);

  std::shared_ptr<PrivRemedy> remedy =
      std::shared_ptr<PrivRemedy>(new PrivRemedy());
  remedy->cost = DEFAULT_PRIV_REMED_COST;
  remedy->type = PrivRemedy::Normal;
  remedy->localPtr = nullptr;

  Remedies Ra, Rb;

  bool privateA = isCheapPrivate(nullptr, P1, L, Ra);
  bool privateB = false;
  if (!privateA) {
    privateB = isCheapPrivate(nullptr, P2, L, Rb);
    if (privateB) {
      for (auto remed : Rb)
        R.insert(remed);
      remedy->privPtr = P2;
    }
  } else {
    for (auto remed : Ra)
      R.insert(remed);
    remedy->privPtr = P1;
  }

  if (!privateA && !privateB)
    return LoopAA::alias(P1, S1, rel, P2, S2, L, R);

  R.insert(remedy);
  return LoopAA::NoAlias;
}

LoopAA::ModRefResult PrivAA::modref(const Instruction *A, TemporalRelation rel,
                                    const Value *ptrB, unsigned sizeB,
                                    const Loop *L, Remedies &R) {

  if (rel == LoopAA::Same)
    return LoopAA::modref(A, rel, ptrB, sizeB, L, R);

  std::shared_ptr<PrivRemedy> remedy =
      std::shared_ptr<PrivRemedy>(new PrivRemedy());
  remedy->cost = DEFAULT_PRIV_REMED_COST;
  remedy->type = PrivRemedy::Normal;
  remedy->localPtr = nullptr;

  Remedies Ra, Rb;
  const Value *ptrA;

  bool privateA = isCheapPrivate(A, ptrA, L, Ra);
  bool privateB = false;
  if (!privateA) {
    privateB = isCheapPrivate(nullptr, ptrB, L, Rb);
    if (privateB) {
      for (auto remed : Rb)
        R.insert(remed);
      remedy->privPtr = ptrB;
    }
  } else {
    for (auto remed : Ra)
      R.insert(remed);
    remedy->privPtr = ptrA;
  }

  if (!privateA && !privateB)
    return LoopAA::modref(A, rel, ptrB, sizeB, L, R);

  R.insert(remedy);
  return LoopAA::NoModRef;
}

LoopAA::ModRefResult PrivAA::modref(const Instruction *A, TemporalRelation rel,
                                    const Instruction *B, const Loop *L,
                                    Remedies &R) {

  if (rel == LoopAA::Same)
    return LoopAA::modref(A, rel, B, L, R);

  std::shared_ptr<PrivRemedy> remedy =
      std::shared_ptr<PrivRemedy>(new PrivRemedy());
  remedy->type = PrivRemedy::Normal;
  remedy->cost = DEFAULT_PRIV_REMED_COST;
  remedy->localPtr = nullptr;

  Remedies Ra, Rb;
  const Value *ptrA, *ptrB;

  bool privateA = isCheapPrivate(A, ptrA, L, Ra);
  bool privateB = false;
  if (!privateA) {
    privateB = isCheapPrivate(B, ptrB, L, Rb);
    if (privateB) {
      for (auto remed : Rb)
        R.insert(remed);
      remedy->privPtr = ptrB;
    }
  } else {
    for (auto remed : Ra)
      R.insert(remed);
    remedy->privPtr = ptrA;
  }

  if (!privateA && !privateB)
    return LoopAA::modref(A, rel, B, L, R);

  // it can disprove deps but not necesserily cheaply
  R.insert(remedy);
  LoopAA::ModRefResult res = LoopAA::NoModRef;
  ++numPrivNoMemDep;

  // check for full-overlap priv
  //
  // need to be loop-carried WAW (two stores involved) where the privitizable
  // store is either A or B
  if (isa<StoreInst>(A) && isa<StoreInst>(B)) {

    if (A != B) {
      // want to cover cases such as this one:
      // for (..)  {
      //   x = ...   (B)
      //   ...
      //   x = ...   (A)
      // }
      // In this case self-WAW of A is killed by B and vice-versa but there is
      // still a LC WAW from A to B. This LC WAW can be ignored provided that B
      // overwrites the memory footprint of A. If not then it is likely that
      // there is no self-WAW LC dep but there is a real non-full-overwrite WAW
      // from A to B. Covariance benchmark from Polybench exhibits this case
      // (A: symmat[j2][j1] = ...  , B: symmat[j1][j2] = 0.0;).
      // Correlation exhibits a similar issue.
      //
      // check that A is killed by B

      if (!L)
        return res;

      if (!ptrA)
        return res;

      const BasicBlock *bbA = A->getParent();
      const BasicBlock *bbB = B->getParent();
      // dominance info are intra-procedural
      if (bbA->getParent() != bbB->getParent())
        return res;
      const DominatorTree *dt = killFlow.getDT(bbA->getParent());

      // collect the chain of all idom from A
      DomTreeNode *nodeA = dt->getNode(const_cast<BasicBlock *>(bbA));
      DomTreeNode *nodeB = dt->getNode(const_cast<BasicBlock *>(bbB));
      if (!nodeA || !nodeB)
        return res;

      std::unordered_set<DomTreeNode *> idomChainA;
      for (DomTreeNode *n = nodeA; n; n = n->getIDom()) {
        const BasicBlock *bb = n->getBlock();
        if (!bb || !L->contains(bb))
          break;
        idomChainA.insert(n);
      }

      const BasicBlock *commonDom = nullptr;
      DomTreeNode *commonDomNode = nullptr;
      for (DomTreeNode *n = nodeB; n; n = n->getIDom()) {
        const BasicBlock *bb = n->getBlock();
        if (!bb || !L->contains(bb))
          break;
        if (idomChainA.count(n)) {
          commonDom = bb;
          commonDomNode = n;
          break;
        }
      }
      if (!commonDom)
        return res;

      if (commonDom == B->getParent()) {
        if (killFlow.instMustKill(B, ptrA, 0, 0, L)) {
          R.erase(remedy);
          remedy->type = PrivRemedy::FullOverlap;
          remedy->cost = FULL_OVERLAP_PRIV_REMED_COST;
          R.insert(remedy);
          return res;
        } else {
          commonDomNode = commonDomNode->getIDom();
          commonDom = commonDomNode->getBlock();
          if (!commonDom || !L->contains(commonDom))
            return res;
        }
      }

      if (!killFlow.blockMustKill(commonDom, ptrA, nullptr, A, 0, 0, L))
        return res;

      // the following check if not enough for correlation
      // if (!killFlow.pointerKilledBefore(L, ptrA, A) &&
      //    !killFlow.pointerKilledBefore(L, ptrB, A))
      //  return res;
      // the following check is too conservative and misses fullOverlap
      // opportunities. Need to use killflow
      // if (!isPointerKillBefore(L, ptrA, A, true))
      //  return res;

      // treat it as full_overlap. if it is not a fullOverlap there will be
      // self-WAW for either A or B that will not be reported as FullOverlap and
      // the underlying AUs will remain in the private family

      R.erase(remedy);
      remedy->type = PrivRemedy::FullOverlap;
      remedy->cost = FULL_OVERLAP_PRIV_REMED_COST;
      R.insert(remedy);
      return res;
    }

    const StoreInst *privStore =
        (privateA) ? dyn_cast<StoreInst>(A) : dyn_cast<StoreInst>(B);

    // evaluate if the private store overwrites the same memory locations and
    // executes the same number of times for every iteration of the loop of
    // interest. If full-overlap is proved for this write then assign
    // PrivRemedy::FullOverlap type to the remedy.

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
      return res;

    if (loopEntryBB->getParent() != privStore->getFunction())
      return res;
    const Loop *innerLoop = nullptr;
    if (pdt->dominates(privStore->getParent(), loopEntryBB)) {
      // private store executes on every iter of loop of interest
    } else {
      // private store does not postdominate loop of interest entry BB.
      // check if private store executes on every iter of an inner loop and the
      // header of inner loop executes on every iter of its parent loop. check
      // same properties on every parent loop until the loop of interest is
      // reached.
      innerLoop = li->getLoopFor(privStore->getParent());
      if (!innerLoop)
        return res;
      if (innerLoop->getHeader()->getParent() != loopEntryBB->getParent())
        return res;
      // check that store executes on every iter of inner loop
      const BasicBlock *innerLoopEntryBB = getLoopEntryBB(innerLoop);
      if (!innerLoopEntryBB)
        return res;
      if (!pdt->dominates(privStore->getParent(), innerLoopEntryBB))
        return res;
      // check that the inner loop that contains the store is a subloop of the
      // loop of interest
      if (!L->contains(innerLoop))
        return res;

      // go over all the parent loops until the loop of interest is reached
      const Loop *parentL = innerLoop->getParentLoop();
      const Loop *childL = innerLoop;
      do {
        if (!parentL)
          return res;
        const BasicBlock *parLEntryBB = getLoopEntryBB(parentL);
        if (!parLEntryBB)
          return res;
        if (childL->getHeader()->getParent() != parLEntryBB->getParent())
          return res;
        if (!pdt->dominates(childL->getHeader(), parLEntryBB))
          return res;
        const Loop *tmpL = parentL;
        parentL = parentL->getParentLoop();
        childL = tmpL;
      } while (childL != L);
    }

    // check if address of store is either a loop-invariant (to the loop of
    // interest), or a gep with only constant or affine SCEVAddRecExpr (to loop
    // with loop-invariant trip counts) indices
    //
    const Value *ptrPrivStore = privStore->getPointerOperand();
    const Loop *scevLoop = nullptr;
    if (L->isLoopInvariant(ptrPrivStore)) {
      // good
    } else if (isa<GlobalValue>(ptrPrivStore)) {
      // good
    } else if (isa<GetElementPtrInst>(ptrPrivStore)) {
      const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptrPrivStore);

      // the base pointer of the gep should be loop-invariant (no support
      // yet for 2D arrays etc.)
      if (!isLoopInvariantValue(gep->getPointerOperand(), L))
        return res;

      // traverse all the indices of the gep, make sure that they are all
      // constant or affine SCEVAddRecExpr (to loops with loop-invariant trip
      // counts, and with loop-invariant step, start and limit/max_val).
      for (auto idx = gep->idx_begin(); idx != gep->idx_end(); ++idx) {
        const Value *idxV = *idx;
        if (L->isLoopInvariant(idxV))
          continue;
        else if (isTransLoopInvariant(idxV, L))
          continue;
        else if (se->isSCEVable(idxV->getType())) {
          if (const SCEVAddRecExpr *addRec = dyn_cast<SCEVAddRecExpr>(
                  se->getSCEV(const_cast<Value *>(idxV)))) {
            if (!addRec)
              return res;
            if (!addRec->isAffine())
              return res;

            if (scevLoop && scevLoop != addRec->getLoop())
              return res;
            scevLoop = addRec->getLoop();
            // if (scevLoop == L || !L->contains(scevLoop))
            if (scevLoop != innerLoop)
              return res;

            // check for loop-invariant offset from base pointer (start, step
            // and loop trip count)

            if (!se->hasLoopInvariantBackedgeTakenCount(scevLoop))
              return res;

            if (!isLoopInvariantSCEV(addRec->getStart(), L, se) ||
                !isLoopInvariantSCEV(addRec->getStepRecurrence(*se), L, se))
              return res;

          } else if (isa<SCEVUnknown>(se->getSCEV(const_cast<Value *>(idxV)))) {
            // detect pseudo-canonical IV (0, +, 1) and return max value
            auto limit = getLimitUnknown(idxV, innerLoop);
            if (!innerLoop || !limit || !isLoopInvariantValue(limit, L))
              return res;
          }

        } else
          return res;
      }
    } else
      return res;

    // success. private store executes same number of times on every loop of
    // interest iter
    R.erase(remedy);
    remedy->type = PrivRemedy::FullOverlap;
    remedy->cost = FULL_OVERLAP_PRIV_REMED_COST;
    R.insert(remedy);
    return res;
  }

  return res;
}

} // namespace liberty
