#define DEBUG_TYPE "priv-remed"

#include "llvm/ADT/Statistic.h"

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"

#include "liberty/Analysis/FindSource.h"
#include "liberty/Orchestration/PrivRemed.h"
#include "liberty/Utilities/GepRange.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/GlobalMalloc.h"
#include "liberty/Utilities/ReachabilityUtil.h"

// conservative privitization in many cases is as expensive as memory versioning
// and locality private. Need to always keep track of who wrote last.
//#define DEFAULT_PRIV_REMED_COST 1
#define DEFAULT_PRIV_REMED_COST 100
#define FULL_OVERLAP_PRIV_REMED_COST 70
#define LOCAL_PRIV_REMED_COST 55

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
  if (this->storeI == privRhs->storeI)
    return this->localPtr < privRhs->localPtr;
  return this->storeI < privRhs->storeI;
}

bool PrivRemediator::mustAlias(const Value *ptr1, const Value *ptr2) {
  // Very easy case
  if (ptr1 == ptr2 && isa<GlobalValue>(ptr1))
    return true;

  PtrsPair key(ptr1, ptr2);
  if (ptrsMustAlias.count(key))
    return ptrsMustAlias[key];

  ptrsMustAlias[key] = loopAA->alias(ptr1, 1, LoopAA::Same, ptr2, 1, 0) == LoopAA::MustAlias;
  return ptrsMustAlias[key];
}

bool PrivRemediator::instMustKill(const Instruction *inst, const Value *ptr,
                                  const Loop *L) {
  const Value *killptr = nullptr;
  if (const StoreInst *store = dyn_cast<StoreInst>(inst)) {
    killptr = store->getPointerOperand();
  } else if (const MemTransferInst *mti = dyn_cast<MemTransferInst>(inst)) {
    killptr = mti->getRawDest();
  }
  if (!killptr)
    return false;

  if (mustAlias(killptr, ptr)) {
    DEBUG(errs() << "There can be no loop-carried flow mem deps to because "
                    "killed by "
                 << *inst << " (Provided that control spec is validated)\n");
    return true;
  }

  return false;
}

bool PrivRemediator::blockMustKill(const BasicBlock *bb, const Value *ptr,
                                   const Instruction *before, const Loop *L) {
  // We try to cache the results.
  // Cache results are only valid if we are going to consider
  // the whole block, i.e. <pt> is not in this basic block.

  const BasicBlock *beforebb = before->getParent();
  BBPtrPair key(bb, ptr);
  if (bbKills.count(key)) {
    if (!bbKills[key]) {
      return false;
    }

    if (bb != beforebb) {
      return bbKills[key];
    }
  }

  // Search this block for any instruction which
  // MUST define the pointer and which happens
  // before.
  for (BasicBlock::const_iterator j = bb->begin(), z = bb->end(); j != z; ++j) {
    const Instruction *inst = &*j;

    if (inst == before)
      break;

    if (!inst->mayWriteToMemory())
      continue;

    // Avoid infinite recursion.
    // Temporarily pessimize this block.
    // We will reassign this more precisely before we return.
    const bool pessimize = !bbKills.count(key);
    if (pessimize)
      bbKills[key] = false;

    const bool iKill = instMustKill(inst, ptr, L);

    // Un-pessimize
    if (pessimize)
      bbKills.erase(key);

    if (iKill) {
      DEBUG(errs() << "\t(in inst " << *inst << ")\n");
      bbKills[key] = true;

      return true;
    }
  }

  if (bb != beforebb)
    bbKills[key] = false;

  return false;
}

bool PrivRemediator::isPointerKillBefore(const Loop *L, const Value *ptr,
                                         const Instruction *before,
                                         bool useCtrlSpec) {
  if (!L)
    return false;

  const BasicBlock *beforebb = before->getParent();
  ControlSpeculation::LoopBlock iter =
      ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(beforebb));

  std::unordered_set<const BasicBlock *> visited;
  while (!iter.isBeforeIteration()) {
    const BasicBlock *bb = iter.getBlock();
    // TODO: not sure why for some loops, this gets stuck within an inner loop
    // and needs to check for visited (dijistra was one of the benchmarks that
    // had this problem)
    // need to check LoopDominators for any potential bug
    if (visited.count(bb))
      break;
    visited.insert(bb);
    if (!bb)
      return false;
    if (!L->contains(bb))
      break;

    if (blockMustKill(bb, ptr, before, L))
      return true;

    if (bb == L->getHeader())
      break;

    ControlSpeculation::LoopBlock next =
        (useCtrlSpec) ? specDT->idom(iter) : noSpecDT->idom(iter);
    if (next == iter)
      // self-loop. avoid infinite loop
      return false;

    iter = next;
  }
  return false;
}

// verify that noone from later iteration reads the written value by this store.
// conservatively ensure that the given store instruction is not part of any
// loop-carried memory flow (RAW) dependences
// If failed to prove statically, try spec check using killflow + ctrl spec
bool PrivRemediator::isPrivate(const Instruction *I, const Loop *L,
                               bool &ctrlSpecUsed) {
  if (!isa<StoreInst>(I))
    return false;
  auto pdgNode = pdg->fetchNode(const_cast<Instruction *>(I));
  for (auto edge : pdgNode->getOutgoingEdges()) {
    if (edge->isLoopCarriedDependence() && edge->isMemoryDependence() &&
        edge->isRAWDependence() && pdg->isInternal(edge->getIncomingT())) {

      // check if LC mem RAW can be removed with killflow + ctrl spec
      LoadInst *loadI = dyn_cast<LoadInst>(edge->getIncomingT());
      if (!loadI)
        return false;
      Value *loadPtr = dyn_cast<Value>(loadI->getPointerOperand());

      if (!isPointerKillBefore(L, loadPtr, loadI))
        return false;

      ctrlSpecUsed = true;
    }
  }
  return true;
}

const Value *getPtr(const Instruction *I, DataDepType dataDepTy) {
  const Value *ptr = liberty::getMemOper(I);
  // if ptr null, check for memcpy/memmove inst.
  // src pointer is read, dst pointer is written.
  // choose pointer for current query based on dataDepTy
  if (!ptr) {
    if (const MemTransferInst *mti = dyn_cast<MemTransferInst>(I)) {
      if (dataDepTy == DataDepType::WAR)
        ptr = mti->getRawSource();
      ptr = mti->getRawDest();
    }
  }
  return ptr;
}

bool PrivRemediator::isLocalPrivate(const Instruction *I, const Value *ptr,
                                    DataDepType dataDepTy, const Loop *L,
                                    bool &ctrlSpecUsed) {
  if (!ptr)
    return false;

  // handle only global pointers for now
  if (auto gv = dyn_cast<GlobalValue>(ptr)) {
    // if global variable is not used outside the loop then it is a local
    if (!localGVs.count(gv)) {
      if (!isGlobalLocalToLoop(gv, L))
        return false;
      localGVs.insert(gv);
      DEBUG(errs() << "Global found to be local: " << *gv << "\n");
    }

    // need to check for private because globals are always initialized. Thus,
    // even without any use outside of the loop they could still assume initial
    // value and at some/all iterations read before writing leading to real RAW
    // deps.
    if (isPrivate(I, L, ctrlSpecUsed)) {
      DEBUG(errs() << "Private store to global: " << *I << "\n");
      return true;
    }
    DEBUG(errs() << "Private check failed for inst: " << *I << "\n");
    return false;
  }
  return false;
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

bool isLoopInvariantValue(const Value *V, const Loop *L) {
  if (L->isLoopInvariant(V)) {
    return true;
  } else if (isTransLoopInvariant(V, L)) {
    return true;
  } else if (const GlobalValue *globalSrc = liberty::findGlobalSource(V)) {
    return isLoopInvariantGlobal(globalSrc, L);
  } else
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
    allLoopInvariant &= isLoopInvariantValue(val, L);
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
  remedy->storeI = nullptr;
  remedy->localPtr = nullptr;
  remedy->ctrlSpecUsed = false;

  // detect private-locals
  if (LoopCarried && L) {
    const Value* ptr1 = getPtr(A, dataDepTy);
    const Value* ptr2 = getPtr(B, dataDepTy);
    if (isLocalPrivate(A, ptr1, dataDepTy, L, remedy->ctrlSpecUsed)) {
      remedResp.depRes = DepResult::NoDep;
      remedy->localPtr = ptr1;
      remedy->storeI = dyn_cast<StoreInst>(A);
      remedy->cost = LOCAL_PRIV_REMED_COST;
      remedy->type = PrivRemedy::Local;
      remedResp.remedy = remedy;
      return remedResp;
    } else if (isLocalPrivate(B, ptr2, dataDepTy, L, remedy->ctrlSpecUsed)) {
      remedResp.depRes = DepResult::NoDep;
      remedy->localPtr = ptr2;
      remedy->storeI = dyn_cast<StoreInst>(B);
      remedy->cost = LOCAL_PRIV_REMED_COST;
      remedy->type = PrivRemedy::Local;
      remedResp.remedy = remedy;
      return remedResp;
    }
  }

  // look for conservative privitization
  bool WAW = dataDepTy == DataDepType::WAW;

  // need to be loop-carried WAW where the privitizable store is either A or B
  bool privateA = isPrivate(A, L, remedy->ctrlSpecUsed);
  bool privateB = false;
  if (!privateA)
    privateB = isPrivate(B, L, remedy->ctrlSpecUsed);
  if (LoopCarried && WAW &&
      ((isa<StoreInst>(A) && privateA) ||
       (isa<StoreInst>(B) && privateB))) {
    ++numPrivNoMemDep;
    remedResp.depRes = DepResult::NoDep;
    if (isa<StoreInst>(A) && privateA)
      remedy->storeI = dyn_cast<StoreInst>(A);
    else
      remedy->storeI = dyn_cast<StoreInst>(B);

    remedy->type = PrivRemedy::Normal;
    remedResp.remedy = remedy;

    DEBUG(errs() << "PrivRemed removed mem dep between inst " << *A << "  and  "
                 << *B << '\n');

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
        return remedResp;
      const Value* ptr1 = getPtr(A, dataDepTy);
      const Value* ptr2 = getPtr(B, dataDepTy);
      if (!ptr1 || !ptr2)
        return remedResp;

      const BasicBlock *bbA = A->getParent();
      const BasicBlock *bbB = B->getParent();
      // dominance info are intra-procedural
      if (bbA->getParent() != bbB->getParent())
        return remedResp;
      const DominatorTree *dt = killFlow.getDT(bbA->getParent());

      // collect the chain of all idom from A
      DomTreeNode *nodeA = dt->getNode(const_cast<BasicBlock *>(bbA));
      DomTreeNode *nodeB = dt->getNode(const_cast<BasicBlock *>(bbB));
      if (!nodeA || !nodeB)
        return remedResp;

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
        return remedResp;

      if (commonDom == B->getParent()) {
        if (killFlow.instMustKill(B, ptr1, 0, 0, L)) {
          remedy->type = PrivRemedy::FullOverlap;
          remedResp.remedy = remedy;
          return remedResp;
        } else {
          commonDomNode = commonDomNode->getIDom();
          commonDom = commonDomNode->getBlock();
          if (!commonDom || !L->contains(commonDom))
            return remedResp;
        }
      }

      if (!killFlow.blockMustKill(commonDom, ptr1, nullptr, A, 0, 0, L))
        return remedResp;

      // the following check if not enough for correlation
      // if (!killFlow.pointerKilledBefore(L, ptr1, A) &&
      //    !killFlow.pointerKilledBefore(L, ptr2, A))
      //  return remedResp;
      // the following check is too conservative and misses fullOverlap
      // opportunities. Need to use killflow
      // if (!isPointerKillBefore(L, ptr1, A, true))
      //  return remedResp;

      // treat it as full_overlap. if it is not a fullOverlap there will be
      // self-WAW for either A or B that will not be reported as FullOverlap and
      // the underlying AUs will remain in the private family
      remedy->type = PrivRemedy::FullOverlap;
      remedResp.remedy = remedy;
      return remedResp;
    }

    const StoreInst *privStore = remedy->storeI;

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
      return remedResp;

    if (loopEntryBB->getParent() != privStore->getFunction())
      return remedResp;
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
        return remedResp;
      if (innerLoop->getHeader()->getParent() != loopEntryBB->getParent())
        return remedResp;
      // check that store executes on every iter of inner loop
      const BasicBlock *innerLoopEntryBB = getLoopEntryBB(innerLoop);
      if (!innerLoopEntryBB)
        return remedResp;
      if (!pdt->dominates(privStore->getParent(), innerLoopEntryBB))
        return remedResp;
      // check that the inner loop that contains the store is a subloop of the
      // loop of interest
      if (!L->contains(innerLoop))
        return remedResp;

      // go over all the parent loops until the loop of interest is reached
      const Loop *parentL = innerLoop->getParentLoop();
      const Loop *childL = innerLoop;
      do {
        if (!parentL)
          return remedResp;
        const BasicBlock *parLEntryBB = getLoopEntryBB(parentL);
        if (!parLEntryBB)
          return remedResp;
        if (childL->getHeader()->getParent() != parLEntryBB->getParent())
          return remedResp;
        if (!pdt->dominates(childL->getHeader(), parLEntryBB))
          return remedResp;
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
    const Loop* scevLoop = nullptr;
    if (L->isLoopInvariant(ptrPrivStore)) {
      // good
    } else if (isa<GlobalValue>(ptrPrivStore)) {
      // good
    } else if (isa<GetElementPtrInst>(ptrPrivStore)) {
      const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptrPrivStore);

      // the base pointer of the gep should be loop-invariant (no support
      // yet for 2D arrays etc.)
      if (!isLoopInvariantValue(gep->getPointerOperand(), L))
        return remedResp;

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
              return remedResp;
            if (!addRec->isAffine())
              return remedResp;

            if (scevLoop && scevLoop != addRec->getLoop())
              return remedResp;
            scevLoop = addRec->getLoop();
            //if (scevLoop == L || !L->contains(scevLoop))
            if (scevLoop != innerLoop)
              return remedResp;

            // check for loop-invariant offset from base pointer (start, step
            // and loop trip count)

            if (!se->hasLoopInvariantBackedgeTakenCount(scevLoop))
              return remedResp;

            if (!isLoopInvariantSCEV(addRec->getStart(), L, se) ||
                !isLoopInvariantSCEV(addRec->getStepRecurrence(*se), L, se))
              return remedResp;

          } else if (isa<SCEVUnknown>(
                         se->getSCEV(const_cast<Value *>(idxV)))) {
            // detect pseudo-canonical IV (0, +, 1) and return max value
            auto limit = getLimitUnknown(idxV, innerLoop);
            if (!innerLoop || !limit || !isLoopInvariantValue(limit, L))
              return remedResp;
          }

        } else
          return remedResp;
      }
    } else
      return remedResp;

    // success. private store executes same number of times on every loop of
    // interest iter
    remedy->type = PrivRemedy::FullOverlap;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
