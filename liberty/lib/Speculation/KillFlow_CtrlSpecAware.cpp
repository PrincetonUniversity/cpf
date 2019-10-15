#define DEBUG_TYPE "kill-flow-aa"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Analysis/AnalysisTimeout.h"
#include "liberty/Analysis/Introspection.h"
#include "liberty/Orchestration/ControlSpecRemed.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"
#include "liberty/Utilities/GepRange.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/GlobalMalloc.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReachabilityUtil.h"

#include <ctime>
#include <cmath>
#include <unordered_set>

#ifndef DEFAULT_CTRL_REMED_COST
#define DEFAULT_CTRL_REMED_COST 45
#endif

namespace liberty
{

using namespace llvm;

STATISTIC(numQueriesReceived,              "Num queries passed to KillFlow_CtrlSpecAware");

STATISTIC(numEligibleBackwardLoadQueries,  "Num eligible BACKWARD LOAD");
STATISTIC(numKilledBackwardLoadFlows,      "Num killed BACKWARD LOAD");

STATISTIC(numEligibleBackwardStoreQueries, "Num eligible BACKWARD STORE");
STATISTIC(numKilledBackwardStore,          "Num killed BACKWARD STORE");

STATISTIC(numEligibleForwardStoreQueries,  "Num eligible FORWARD STORE");
STATISTIC(numKilledForwardStoreFlows,      "Num killed FORWARD STORE");

STATISTIC(numEligibleForwardLoadQueries,   "Num eligible FORWARD LOAD");
STATISTIC(numKilledForwardLoad,            "Num killed FORWARD LOAD");

STATISTIC(numEligibleBackwardCallQueries,  "Num eligible BACKWARD CALLSITE");
// STATISTIC(numKilledBackwardCallFlows,      "Num killed BACKWARD CALLSITE");
STATISTIC(numEligibleForwardCallQueries,   "Num eligible FORWARD CALLSITE");
//STATISTIC(numKilledForwardCallFlows,       "Num killed FORWARD CALLSITE");

STATISTIC(numSubQueries,                   "Num sub-queries spawned");
STATISTIC(numFcnSummaryHits,               "Number of function summary hits");
STATISTIC(numBBSummaryHits,                "Number of block summary hits");

  const PostDominatorTree *KillFlow_CtrlSpecAware::getPDT(const Function *cf)
  {
    Function *f = const_cast< Function * >(cf);
    PostDominatorTree *pdt = & mloops->getAnalysis_PostDominatorTree(f);
    return pdt;
  }

  const DominatorTree *KillFlow_CtrlSpecAware::getDT(const Function *cf)
  {
    Function *f = const_cast< Function * >(cf);
    DominatorTree *dt = & mloops->getAnalysis_DominatorTree(f);
    return dt;
  }

  ScalarEvolution *KillFlow_CtrlSpecAware::getSE(const Function *cf)
  {
    Function *f = const_cast< Function * >(cf);
    ScalarEvolution *se = & mloops->getAnalysis_ScalarEvolution(f);
    return se;
  }

  LoopInfo *KillFlow_CtrlSpecAware::getLI(const Function *cf)
  {
    Function *f = const_cast< Function * >(cf);
    LoopInfo *li = & mloops->getAnalysis_LoopInfo(f);
    return li;
  }

  bool KillFlow_CtrlSpecAware::mustAlias(const Value *storeptr, const Value *loadptr)
  {
    // Very easy case
    if( storeptr == loadptr && isa< GlobalValue >(storeptr) )
      return true;

    Remedies R;

    LoopAA *top = getEffectiveTopAA();
    ++numSubQueries;
    return top->alias(storeptr,1, Same, loadptr,1, 0, R) == LoopAA::MustAlias;
  }

  /// Non-topping case of pointer comparison.
  bool KillFlow_CtrlSpecAware::mustAliasFast(const Value *storeptr, const Value *loadptr, const DataLayout &DL)
  {
    UO a, b;
    GetUnderlyingObjects(storeptr,a,DL);
    if( a.size() != 1 )
      return false;
    GetUnderlyingObjects(loadptr,b,DL);
    return a == b;
  }


  bool KillFlow_CtrlSpecAware::instMustKill(const Instruction *inst, const Value *ptr, time_t queryStart, unsigned Timeout, const Loop *L)
  {
//    INTROSPECT(
//      errs() << "\t\t\t\tinstMustKill(" << *inst << "):\n");

    // llvm.lifetime.start, llvm.lifetime.end are intended to limit
    // the lifetime of memory objects.  They are especially powerful
    // for alloca's that were inlined, since the alloca's can be moved
    // to the caller's header.  We model them as storing an undef
    // value to the memory location.
    if( const IntrinsicInst *intrinsic = dyn_cast<IntrinsicInst>(inst) )
    {
      if( intrinsic->getIntrinsicID() == Intrinsic::lifetime_start
      ||  intrinsic->getIntrinsicID() == Intrinsic::lifetime_end )
      {
        Value *lifeptr = intrinsic->getArgOperand(1);

        //sot
        const Module *M = inst->getModule();
        const DataLayout &DL = M->getDataLayout();

        // lifetime intrinsics are usually only applied to allocas;
        // don't do a full-on top query to compare.
        if( mustAliasFast(lifeptr, ptr, DL) )
        {
          DEBUG(errs() << "Killed by " << *intrinsic << '\n');
          return true;
        }
        return false;
      }
    }

    CallSite cs = getCallSite(inst);
    if( cs.getInstruction() && cs.getCalledFunction() )
    {
      Function *f = cs.getCalledFunction();

      if( f->isDeclaration() )
        return false;

      FcnPtrPair key(f,ptr);
      if( fcnKills.count(key) )
      {
        ++numFcnSummaryHits;
        return fcnKills[key];
      }

      // basic blocks which post-dominate
      // the function entry.  These are the blocks which
      // MUST execute every time the function is invoked.
      const PostDominatorTree *pdt = getPDT(f);
      DomTreeNode *start = pdt->getNode( &f->front() );
      assert(start && "No postdomtree node for block?");
      for(DomTreeNode *n=start; n; n=n->getIDom() )
      {
        const BasicBlock *bb = n->getBlock();
        if( !bb )
          break;

        if( blockMustKill(bb,ptr,0,0, queryStart, Timeout, L) )
        {
          // Memoize for later.
          fcnKills[key] = true;

          DEBUG(errs() << "\t(in block " << *bb << ")\n");
//          INTROSPECT(errs() << "\tYes\n");
          return true;
        }
      }

      fcnKills[key] = false;
//      INTROSPECT(errs() << "\tNo\n");
      return false;
    }

    if( const StoreInst *store = dyn_cast< StoreInst >(inst) )
    {
      const Value *storeptr = store->getPointerOperand();

      if( mustAlias(storeptr, ptr) )
      {
        DEBUG(errs() << "There can be no loop-carried flow mem deps to because killed by " << *store << '\n');
        return true;
      }
    }

    return false;
  }

  void KillFlow_CtrlSpecAware::uponStackChange()
  {
    fcnKills.clear();
    bbKills.clear();
    noStoresBetween.clear();
  }

  BasicBlock *KillFlow_CtrlSpecAware::getLoopEntryBB(const Loop *loop) {
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

  bool KillFlow_CtrlSpecAware::aliasBasePointer(const Value *gepptr, const Value *killgepptr,
                                  const GlobalValue **gvSrc,
                                  ScalarEvolution *se, const Loop *L) {
    if (killgepptr == gepptr)
      return true;

    if (mustAlias(killgepptr, gepptr))
      return true;

    if (!isa<Instruction>(gepptr) || !isa<Instruction>(killgepptr))
      return false;

    auto gepptrI = dyn_cast<Instruction>(gepptr);
    auto killgepptrI = dyn_cast<Instruction>(killgepptr);
    if (!L->contains(gepptrI) || !L->contains(killgepptrI))
      return false;

    const GlobalValue *globalSrc = liberty::findGlobalSource(gepptr);
    const GlobalValue *killGlobalSrc = liberty::findGlobalSource(killgepptr);
    if (globalSrc != killGlobalSrc)
      return false;
    else if (globalSrc) {
      *gvSrc = globalSrc;
      // check if globalSrc is loop-invariant (no defs inside the loop)
      return isLoopInvariantGlobal(globalSrc, L);
    }
    return false;
  }

  bool KillFlow_CtrlSpecAware::greaterThan(const SCEV *killTripCount, const SCEV *tripCount,
                             ScalarEvolution *se, const Loop *L) {

    const SCEV *tripCountDiff = se->getMinusSCEV(killTripCount, tripCount);
    const ConstantRange tripCountDiffRange = se->getSignedRange(tripCountDiff);
    if (tripCountDiffRange.getSignedMin().sge(0))
      return true;

    auto *killSMax = dyn_cast<SCEVSMaxExpr>(killTripCount);
    auto *sMax = dyn_cast<SCEVSMaxExpr>(tripCount);

    if (killSMax && sMax) {

      const SCEVConstant *killSMax0 =
          dyn_cast<SCEVConstant>(killSMax->getOperand(0));
      const SCEVConstant *sMax0 = dyn_cast<SCEVConstant>(sMax->getOperand(0));

      if (!killSMax0 || !sMax0)
        return false;

      // make sure that lower limit of trip count is the same (usually zero)
      const SCEV *maxV0Diff = se->getMinusSCEV(killSMax0, sMax0);
      if (const SCEVConstant *maxV0DiffConst =
              dyn_cast<SCEVConstant>(maxV0Diff)) {
        if (maxV0DiffConst->getAPInt() != 0)
          return false;
      } else
        return false;

      // then compare the values of the dynamic limit
      const SCEVUnknown *killSMax1 = nullptr;
      const SCEVUnknown *sMax1 = nullptr;
      if (auto *vcastKill = dyn_cast<SCEVCastExpr>(killSMax->getOperand(1)))
        killSMax1 = dyn_cast<SCEVUnknown>(vcastKill->getOperand());
      else
        killSMax1 = dyn_cast<SCEVUnknown>(killSMax->getOperand(1));

      if (auto *vcast = dyn_cast<SCEVCastExpr>(sMax->getOperand(1)))
        sMax1 = dyn_cast<SCEVUnknown>(vcast->getOperand());
      else
        sMax1 = dyn_cast<SCEVUnknown>(sMax->getOperand(1));

      if (!killSMax1 || !sMax1)
        return false;

      const Value *killLimit = killSMax1->getValue();
      const Value *limit = sMax1->getValue();
      if (!killLimit || !limit)
        return false;

      if (killLimit == limit)
        return true;

      if (isa<ConstantInt>(killLimit) && isa<ConstantInt>(limit)) {
        const ConstantInt *ciLimit = dyn_cast<ConstantInt>(limit);
        const ConstantInt *ciKillLimit = dyn_cast<ConstantInt>(killLimit);
        return (ciLimit->getSExtValue() <= ciKillLimit->getSExtValue());
      }

      if (!isa<Instruction>(killLimit) || !isa<Instruction>(limit))
        return false;

      auto limitI = dyn_cast<Instruction>(limit);
      auto killLimitI = dyn_cast<Instruction>(killLimit);
      if (!L->contains(limitI) || !L->contains(killLimitI))
        return false;

      const GlobalValue *globalSrc = liberty::findGlobalSource(limit);
      const GlobalValue *killGlobalSrc = liberty::findGlobalSource(killLimit);
      if (globalSrc != killGlobalSrc)
        return false;
      else if (globalSrc) {
        return isLoopInvariantGlobal(globalSrc, L);
      }
    }
    return false;
  }

  bool KillFlow_CtrlSpecAware::killAllIdx(const Value *killidx, const Value *basePtr,
                            const GlobalValue *baseGVSrc, ScalarEvolution *se,
                            const Loop *L, const Loop *innerL,
                            unsigned idxID) {

    // Examine if killidx goes across the whole allocation unit; if
    // that's the case then no need to check the dominated idx, it will be
    // contained in one of the indexes of the kill.
    const PointerType *basePtrTy = dyn_cast<PointerType>(basePtr->getType());
    if (basePtrTy && basePtrTy->getElementType()->isArrayTy() && idxID == 1) {
      ArrayType *auType = dyn_cast<ArrayType>(basePtrTy->getElementType());
      uint64_t numOfElemInAU = auType->getNumElements();
      // check that the killidx starts from 0 up to numOfElemInAU with step 1

      if (!se->isSCEVable(killidx->getType()))
        return false;
      auto *killAddRec =
          dyn_cast<SCEVAddRecExpr>(se->getSCEV(const_cast<Value *>(killidx)));
      if (!killAddRec)
        return false;

      const Value *killLimit = getCanonicalRange(killAddRec, innerL, se);
      if (!killLimit)
        return false;

      if (const ConstantInt *ciKillLimit = dyn_cast<ConstantInt>(killLimit)) {
        if (ciKillLimit->getZExtValue() == numOfElemInAU)
          return true;
        else if (ciKillLimit->getZExtValue() == numOfElemInAU - 1) {
          // check if there is a write to the same base pointer at index
          // numOfElemInAU - 1 in the loop exit BB
          const BasicBlock *exitBB = innerL->getExitBlock();
          if (!exitBB)
            return false;
          for (const Instruction &exitI : *exitBB) {
            if (auto exitS = dyn_cast<StoreInst>(&exitI)) {
              if (const GetElementPtrInst *exitGep =
                      dyn_cast<GetElementPtrInst>(exitS->getPointerOperand())) {

                const GlobalValue *gvSrc = nullptr;
                auto exitGepBasePtr = exitGep->getPointerOperand();
                auto exitGepBasePtrTy = exitGep->getPointerOperandType();
                if (exitGep->hasAllConstantIndices() &&
                    exitGep->getNumIndices() == 2 &&
                    exitGepBasePtrTy == basePtrTy &&
                    aliasBasePointer(exitGepBasePtr, basePtr, &gvSrc, se, L)) {
                  // first idx should be equal to zero, second should equal to
                  // numOfElemInAU - 1
                  auto firstIdxC = dyn_cast<ConstantInt>(exitGep->idx_begin());
                  auto secondIdxC =
                      dyn_cast<ConstantInt>(exitGep->idx_begin() + 1);
                  if (firstIdxC && firstIdxC->getZExtValue() == 0 &&
                      secondIdxC &&
                      secondIdxC->getZExtValue() == numOfElemInAU - 1) {
                    loopKillAlongInsts[innerL->getHeader()].insert(&exitI);
                    return true;
                  }
                }
              }
            }
          }
          return false;
        } else
          return false;
      }
      return false;
    }

    if (idxID || !baseGVSrc)
      return false;

    killidx = bypassExtInsts(killidx);

    if (!se->isSCEVable(killidx->getType()))
      return false;

    // checks for base ptr type
    const PointerType *baseObjTy = dyn_cast<PointerType>(basePtr->getType());
    if (!baseObjTy)
      return false;
    const Type *elemObjTy = baseObjTy->getElementType();

    if (!elemObjTy->isIntegerTy() && !elemObjTy->isFloatingPointTy() &&
        !elemObjTy->isPointerTy() && !elemObjTy->isStructTy()) {
      // we do not handle other types
      return false;
    }

    const Type *type = baseGVSrc->getType()->getElementType();
    if (!type->isPointerTy())
      return false;
    const Type *elemType = (dyn_cast<PointerType>(type))->getElementType();

    if (elemType != elemObjTy)
      // if (elemType != privObj->getType())
      return false;

    if (!elemType->isSized())
      return false;
    uint64_t gvElemSize = DL->getTypeSizeInBits(const_cast<Type *>(elemType));
    if (gvElemSize == 0)
      return false;
    // bits->bytes
    gvElemSize /= 8;

    const Value *killLimit = nullptr;
    if (isa<SCEVUnknown>(se->getSCEV(const_cast<Value *>(killidx)))) {
      killLimit = getLimitUnknown(killidx, innerL);
    } else if (auto *killAddRec = dyn_cast<SCEVAddRecExpr>(
                   se->getSCEV(const_cast<Value *>(killidx)))) {
      killLimit = getCanonicalRange(killAddRec, innerL, se);
    }
    if (!killLimit)
      return false;

    std::vector<const Instruction *> mallocSrcs;
    bool noCaptureMallocOnly =
        findNoCaptureGlobalMallocSrcs(baseGVSrc, mallocSrcs, tli);
    if (!noCaptureMallocOnly || mallocSrcs.empty())
      return false;

    // examine if full overlap for all the malloc sources
    for (auto srcI : mallocSrcs) {
      // TODO: check if there is a path from srcI to ptrPrivStore. not
      // checking is more conservative

      // if (L->contains(srcI))
      //  return false;

      const Value *numOfElem = nullptr;
      uint64_t sizeOfElem;
      findAllocSizeInfo(srcI, &numOfElem, sizeOfElem);
      if (sizeOfElem == 0)
        return false;
      if (numOfElem == nullptr)
        return false;

      // check that sizeOfElem in malloc matches the one of the global
      // pointer which is the base pointer of the store
      if (sizeOfElem != gvElemSize)
        return false;

      if (killLimit != numOfElem) {

        if (isa<ConstantInt>(killLimit) && isa<ConstantInt>(numOfElem)) {
          const ConstantInt *ciLimit = dyn_cast<ConstantInt>(numOfElem);
          const ConstantInt *ciKillLimit = dyn_cast<ConstantInt>(killLimit);
          if (ciLimit->getSExtValue() != ciKillLimit->getSExtValue())
            return false;
          continue;
        }

        // if both of them are loads from a global check that there is no
        // store in between them (aka both loads return the same value)

        if (!isa<Instruction>(killLimit) || !isa<Instruction>(numOfElem))
          return false;
        const Instruction *killLimitI = dyn_cast<Instruction>(killLimit);
        const Instruction *numOfElemI = dyn_cast<Instruction>(numOfElem);

        const GlobalValue *numOfElemGV = liberty::findGlobalSource(numOfElem);
        const GlobalValue *killLimitGV = liberty::findGlobalSource(killLimit);
        if (numOfElemGV != killLimitGV || !killLimitGV)
          return false;

        std::vector<const Instruction *> srcs;
        bool noCaptureGV = findNoCaptureGlobalSrcs(killLimitGV, srcs);
        if (!noCaptureGV)
          return false;

        // if zero initialized and only one def, then numOfElemI and killLimitI
        // got the same value
        bool zeroInitialized = false;
        if (const GlobalVariable *gVar = dyn_cast<GlobalVariable>(killLimitGV))
          zeroInitialized = gVar->getInitializer()->isZeroValue();

        // we need to ensure that there is no store to the global between
        // numOfElem and killLimit
        if ((!zeroInitialized || srcs.size() != 1)) {
          InstPtrPair ikey(numOfElemI, killLimitI);
          bool noStoreBetween;
          if (noStoresBetween.count(ikey)) {
            noStoreBetween = noStoresBetween[ikey];
          } else {
            const Instruction *noExtNumOfElemI =
                dyn_cast<Instruction>(bypassExtInsts(numOfElemI));
            const Instruction *noExtKillLimitI =
                dyn_cast<Instruction>(bypassExtInsts(killLimitI));
            if (!noExtNumOfElemI || !noExtKillLimitI)
              return false;
            noStoreBetween = noStoreInBetween(noExtNumOfElemI, noExtKillLimitI,
                                              srcs, *mloops);
            noStoresBetween[ikey] = noStoreBetween;
          }
          if (!noStoreBetween)
            return false;
        }
      }
    }
    return true;
  }

  // kill idx should be equal or include the other inst's idx
  bool KillFlow_CtrlSpecAware::matchingIdx(const Value *idx, const Value *killidx,
                             ScalarEvolution *se, const Loop *L) {
    if (idx == killidx)
      return true;

    if (isa<ConstantInt>(idx) && isa<ConstantInt>(killidx)) {
      const ConstantInt * ciIdx = dyn_cast<ConstantInt>(idx);
      const ConstantInt * ciKillIdx = dyn_cast<ConstantInt>(killidx);
      return (ciIdx->getSExtValue() == ciKillIdx->getSExtValue());
    }

    idx = bypassExtInsts(idx);
    killidx = bypassExtInsts(killidx);
    if (idx == killidx)
      return true;

    if (se->isSCEVable(killidx->getType())) {
      auto *killAddRec =
          dyn_cast<SCEVAddRecExpr>(se->getSCEV(const_cast<Value *>(killidx)));
      const SCEVAddRecExpr *addRec = nullptr;
      if (se->isSCEVable(idx->getType()))
        addRec =
            dyn_cast<SCEVAddRecExpr>(se->getSCEV(const_cast<Value *>(idx)));

      if (killAddRec && addRec) {
        if (!killAddRec->isAffine() || !addRec->isAffine())
          return false;

        if (!se->hasLoopInvariantBackedgeTakenCount(killAddRec->getLoop()) ||
            !se->hasLoopInvariantBackedgeTakenCount(addRec->getLoop()))
          return false;

        const SCEV *killStart = killAddRec->getOperand(0);
        const SCEV *start = addRec->getOperand(0);
        const SCEV *startDiff = se->getMinusSCEV(killStart, start);
        if (const SCEVConstant *startDiffConst = dyn_cast<SCEVConstant>(startDiff)) {
          if (startDiffConst->getAPInt() != 0)
            return false;
        }
        else
          return false;

        const SCEV *killStep = killAddRec->getOperand(1);
        const SCEV *step = addRec->getOperand(1);
        const SCEV *stepDiff = se->getMinusSCEV(killStep, step);
        if (const SCEVConstant *stepDiffConst = dyn_cast<SCEVConstant>(stepDiff)) {
          if (stepDiffConst->getAPInt() != 0)
            return false;
        }
        else
          return false;

        const SCEV *killTripCount =
            se->getBackedgeTakenCount(killAddRec->getLoop());
        const SCEV *tripCount = se->getBackedgeTakenCount(addRec->getLoop());

        if (greaterThan(killTripCount, tripCount, se, L))
          return false;

        return true;
      }
    }
    return false;
  }

  /// Determine if the block MUST KILL the specified pointer.
  /// If <after> belongs to this block and <after> is not null, only consider operations AFTER <after>
  /// If <after> belongs to this block and <before> is is not null, only consider operations BEFORE <before>
  bool KillFlow_CtrlSpecAware::blockMustKill(const BasicBlock *bb, const Value *ptr, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout, const Loop *L)
  {
    const BasicBlock *beforebb = before ? before->getParent() : 0;
    const BasicBlock *afterbb  = after  ? after->getParent() : 0;

    // We try to cache the results.
    // Cache results are only valid if we are going to consider
    // the whole block, i.e. <pt> is not in this basic block.


    BBPtrPair key(bb,ptr);

    // for pointerKilledBefore queries we allow the use of the ptr of the other
    // instruction instead of solely the ptr of the before inst
    // (see Backward kills for loop-carried queries to modref)
    if (!after)
      key.second = before;


    if( bbKills.count(key) )
    {
      if( !bbKills[key] )
      {
        ++numBBSummaryHits;
        return false;
      }

      if( bb != beforebb && bb != afterbb)
      {
        ++numBBSummaryHits;
        return bbKills[key];
      }
    }

    // Search this block for any instruction which
    // MUST define the pointer and which happens
    // before.
    bool start = (afterbb != bb);
    for(BasicBlock::const_iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      const Instruction *inst = &*j;

      if( !start )
      {
        if( inst == after )
          start = true;
        continue;
      }

      if( inst == before )
        break;

      if( !inst->mayWriteToMemory() )
        continue;

      // Avoid infinite recursion.
      // Temporarily pessimize this block.
      // We will reassign this more precisely before we return.
      const bool pessimize = !bbKills.count(key);
      if( pessimize ) bbKills[key] = false;

      const bool iKill = instMustKill(inst, ptr, queryStart, Timeout, L);

      // Un-pessimize
      if( pessimize ) bbKills.erase(key);

      if( iKill )
      {
        DEBUG(errs() << "\t(in inst " << *inst << ")\n");
        bbKills[key] = true;

        return true;
      }
    }

    //if (bb != beforebb && bb != afterbb && isa<GetElementPtrInst>(ptr) && L) {
    if (bb != beforebb && bb != afterbb && L) {
      LoopInfo *li = getLI(bb->getParent());

      if (li->isLoopHeader(bb)) {
        Loop *innerLoop = li->getLoopFor(bb);

        if ((after && innerLoop->contains(after)) ||
            (before && innerLoop->contains(before)))
          // do not update the bbKills cache in this case
          return false;

        const BasicBlock *innerLHeader = innerLoop->getHeader();
        if (loopKillAlongInsts.count(innerLHeader)) {
          auto killAlongInsts = &loopKillAlongInsts[innerLHeader];
          if ((before && killAlongInsts->count(before)) ||
              (after && killAlongInsts->count(after)))
            return false;
        }

        ScalarEvolution *se = getSE(bb->getParent());

        for (auto loopBB : innerLoop->getBlocks()) {
          // check that loopBB postdominates the entry BB of inner loop
          // (postdominator is intraprocedural)
          const BasicBlock *innerLoopEntryBB = getLoopEntryBB(innerLoop);
          if (!innerLoopEntryBB ||
              (innerLoopEntryBB->getParent() != loopBB->getParent()))
            return false;
          auto pdt = getPDT(innerLoopEntryBB->getParent());
          if (!pdt->dominates(loopBB, innerLoopEntryBB))
            return false;

          for (BasicBlock::const_iterator k = loopBB->begin(),
                                          e = loopBB->end();
               k != e; ++k) {
            const Instruction *loopInst = &*k;

            if (const StoreInst *store = dyn_cast<StoreInst>(loopInst)) {
              const Value *storeptr = store->getPointerOperand();

              if (const GetElementPtrInst *killgep =
                      dyn_cast<GetElementPtrInst>(storeptr)) {

                const Value *killgepptr = killgep->getPointerOperand();
                unsigned killNumIndices = killgep->getNumIndices();

                const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptr);
                if (!gep)
                  return false;
                const Value *gepptr = gep->getPointerOperand();
                unsigned numIndices = gep->getNumIndices();

                if (killNumIndices != numIndices)
                  continue;

                const GlobalValue *gvSrc = nullptr;
                if (!aliasBasePointer(gepptr, killgepptr, &gvSrc, se, L))
                  continue;

                bool iKill = true;
                auto killidx = killgep->idx_begin();
                unsigned idxID = 0;
                for (auto idx = gep->idx_begin(); idx != gep->idx_end();
                     ++idx) {

                  if (!matchingIdx(*idx, *killidx, se, L) &&
                      !killAllIdx(*killidx, killgepptr, gvSrc, se, L, innerLoop,
                                  idxID)) {
                    iKill = false;
                    break;
                  }
                  ++idxID;
                  ++killidx;
                }

                if (loopKillAlongInsts.count(innerLHeader)) {
                  auto killAlongInsts = &loopKillAlongInsts[innerLHeader];
                  if ((before && killAlongInsts->count(before)) ||
                      (after && killAlongInsts->count(after)))
                    return false;
                }

                if (iKill) {
                  DEBUG(errs() << "\t(in inst " << *loopInst << ")\n");
                  // bbKills[key] = true;

                  DEBUG(errs() << "There can be no loop-carried flow mem "
                                  "deps to because killed by "
                               << *store << '\n');
                  return true;
                }
              }
            }
          }
        }
      }
      // bbKills[key] = false in some cases breaks the correctness
      // TODO: figure out how to memoize all the false bbKills without
      // affecting correctness!
      return false;
    }

    if( bb != beforebb && bb != afterbb )
      bbKills[key] = false;

    return false;
  }


  std::set<Instruction *> instLis;
  bool KillFlow_CtrlSpecAware::allLoadsAreKilledBefore(const Loop *L, CallSite &cs, time_t queryStart, unsigned Timeout)
  {
    Function *fcn = cs.getCalledFunction();

    for(inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      Instruction *inst = &*i;
      if( !inst->mayReadFromMemory() )
        continue;

      if( LoadInst *load = dyn_cast< LoadInst >(inst) )
        if( pointerKilledBefore(0, load->getPointerOperand(), load)
        ||  pointerKilledBefore(L, load->getPointerOperand(), cs.getInstruction() ) )
          continue;

      CallSite cs2 = getCallSite(inst);
      if( cs2.getInstruction() )
        if( Function *f2 = cs2.getCalledFunction() )
          if( !f2->isDeclaration() )
          {
            if(instLis.count(inst)==0)
            {
              instLis.insert(inst);
              if( allLoadsAreKilledBefore(L,cs2,queryStart,Timeout) )
              {
                instLis.erase(inst);
                continue;
              }
              instLis.erase(inst);
            }
          }

      DEBUG(errs() << "\tbut " << *inst << " ruins it for this callsite.\n");
      return false;
    }

    return true;
  }

  KillFlow_CtrlSpecAware::KillFlow_CtrlSpecAware()
      : ModulePass(ID), fcnKills(), bbKills(), noStoresBetween(), mloops(0),
        effectiveNextAA(0), effectiveTopAA(0), specDT{nullptr}, specPDT{nullptr}, tgtLoop{nullptr} {}

  KillFlow_CtrlSpecAware::~KillFlow_CtrlSpecAware() {}

  LoopAA::ModRefResult KillFlow_CtrlSpecAware::modref(const Instruction *i1,
                      LoopAA::TemporalRelation Rel,
                      const Instruction *i2,
                      const Loop *L, Remedies &R)
  {
    INTROSPECT(ENTER(i1,Rel,i2,L));
    ++numQueriesReceived;

    ModRefResult res = ModRef;

    // Do not want to pollute remedies with more expensive-to-validate modules
    // chain after trying to respond
    /*
    ModRefResult res = getEffectiveNextAA()->modref(i1,Rel,i2,L,R);
    INTROSPECT(errs() << "lower in the stack reports res=" << res << '\n');
    if( res == Ref || res == NoModRef )
    {
      INTROSPECT(EXIT(i1,Rel,i2,L,res));
      return res;
    }
    */

    /* TODO: For using ModuleLoops, convert the L here to the ModuleLoops L */
    // Since we are now using ModuleLoops and we could have potentially been
    // called from a  dirty LLVM loop pointer,
    // we need to be sure to translate into ModuleLoop
    if(L)
    {
      LoopInfo &LI = mloops->getAnalysis_LoopInfo(L->getHeader()->getParent());
      L = LI.getLoopFor(L->getHeader());
    }

    if( !L )
    {
      INTROSPECT(EXIT(i1,Rel,i2,L,res));
      return getEffectiveNextAA()->modref(i1,Rel,i2,L,R);
    }

    std::shared_ptr<ControlSpecRemedy> remedy =
        std::shared_ptr<ControlSpecRemedy>(new ControlSpecRemedy());
    remedy->cost = DEFAULT_CTRL_REMED_COST;
    remedy->brI = nullptr;

    // use ptr1 & ptr2 for load/store cases
    const Value *ptr1 = liberty::getMemOper(i1);
    const Value *ptr2 = liberty::getMemOper(i2);
    // handle cases where the gep is bitcasted before the mem operation
    if (ptr1) {
      while (auto cast1 = dyn_cast<CastInst>(ptr1))
        ptr1 = cast1->getOperand(0);
    }
    if (ptr2) {
      while (auto cast2 = dyn_cast<CastInst>(ptr2))
        ptr2 = cast2->getOperand(0);
    }

    if( Rel == Same )
    {
      if( L->contains(i1) && L->contains(i2) )
      {
        // intra-iteration reachability is checked before initiating an
        // intra-iteration mem dep query
        /*
        // Is there a path from i1 to i2 within this loop?
        if( !isReachable(L,i1,i2) )
        {
          res = NoModRef;
        }
        else
        */
        {
          if (isa<StoreInst>(i1)) {
            ++numEligibleForwardStoreQueries;
            //if( pointerKilledBetween(L,store->getPointerOperand(),i1,i2) )
            if( pointerKilledBetween(L,ptr1,i1,i2) )
            {
              ++numKilledForwardStoreFlows;
              //res = ModRefResult(res & ~Mod);
              R.insert(remedy);
              res = NoModRef;
            }
          }

          if (isa<LoadInst>(i2)) {
            ++numEligibleBackwardLoadQueries;
            //if( pointerKilledBetween(L,load->getPointerOperand(),i1,i2) )
            if( pointerKilledBetween(L,ptr2,i1,i2) )
            {
              ++numKilledBackwardLoadFlows;
              //res = ModRefResult(res & ~Mod);
              R.insert(remedy);
              res = NoModRef;
            }
          }

          // handle WAR
          if (isa<LoadInst>(i1)) {
            ++numEligibleForwardLoadQueries;
            //if( pointerKilledBetween(L,load->getPointerOperand(),i1,i2) )
            if( pointerKilledBetween(L,ptr1,i1,i2) )
            {
              ++numKilledForwardLoad;
              R.insert(remedy);
              res = NoModRef;
            }
          }

          // handle WAW
          if (isa<StoreInst>(i2)) {
            ++numEligibleBackwardStoreQueries;
            //if( pointerKilledBetween(L,store->getPointerOperand(),i1,i2) )
            if( pointerKilledBetween(L,ptr2,i1,i2) )
            {
              ++numKilledBackwardStore;
              R.insert(remedy);
              res = NoModRef;
            }
          }
        }
      }
      INTROSPECT(EXIT(i1,Rel,i2,L,res));
      if (res != NoModRef) {
        Remedies tmpR;
        ModRefResult nextRes =
            getEffectiveNextAA()->modref(i1, Rel, i2, L, tmpR);
        if (ModRefResult(res & nextRes) != res) {
          for (auto remed : tmpR)
            R.insert(remed);
          res = ModRefResult(res & nextRes);
        }
      }
      return res;
    }

    const Instruction *earlier = i1, *later = i2;
    const Value *earlierPtr = ptr1, *laterPtr = ptr2;
    if( Rel == After ) {
      std::swap(earlier,later);
      std::swap(earlierPtr, laterPtr);
    }

    time_t queryStart=0;
    if( AnalysisTimeout > 0 )
      time(&queryStart);

    // Backward kills:
    //  Can this operation read a value from a previous iteration.
    //  We must check if there is an earlier operation in this
    //  loop which MUST define it, thus killing the flow.
    if( L->contains(later) )
    {
      if( const LoadInst *load = dyn_cast< LoadInst >(later) )
      {
        ++numEligibleBackwardLoadQueries;
        //if( pointerKilledBefore(L, load->getPointerOperand(), load, queryStart, AnalysisTimeout) )
        if( pointerKilledBefore(L, laterPtr, load, queryStart, AnalysisTimeout) )
        {
          ++numKilledBackwardLoadFlows;
          DEBUG(errs() << "Removed the mod bit at AAA\n");
          // res = ModRefResult(res & ~Mod);
          R.insert(remedy);
          res = NoModRef;
        }

        // check whether the pointer of the earlier op (from previous iter) is
        // killed before the load
        else if (earlierPtr &&
                 pointerKilledBefore(L, earlierPtr, load, queryStart,
                                     AnalysisTimeout)) {
          ++numKilledBackwardLoadFlows;
          DEBUG(errs() << "Removed the mod bit at AAA\n");
          // res = ModRefResult(res & ~Mod);
          R.insert(remedy);
          res = NoModRef;
        }
      }


      CallSite cs = getCallSite(later);
      if( cs.getInstruction() )
        if( Function *f = cs.getCalledFunction() )
          if( !f->isDeclaration() )
          {
            ++numEligibleBackwardCallQueries;

/* Evidence suggests that this does not improve AA performance,
 * but takes a long time:
 */
/* 12 Sep 2011: Stephen found a bug that is triggered by this line.
 * I'm going to remove it. - Nick
 */
/*
            if( allLoadsAreKilledBefore(L, cs) )
            {
              ++numKilledBackwardCallFlows;
              INTROSPECT(errs() << "Removed the mod bit at BBB\n");
              res = ModRefResult(res & ~Mod);
            }
 */
          }

      // handle WAR and WAW
      if( const StoreInst *store = dyn_cast< StoreInst >(later) )
      {
        ++numEligibleBackwardStoreQueries;
        //if( pointerKilledBefore(L, store->getPointerOperand(), store, queryStart, AnalysisTimeout) )
        if( pointerKilledBefore(L, laterPtr, store, queryStart, AnalysisTimeout) )
        {
          ++numKilledBackwardStore;
          // no dependence between this store and insts from previous iteration possible
          R.insert(remedy);
          res = NoModRef;
        }

        // check whether the pointer of the earlier op (from previous iter) is
        // killed before the store
        else if (earlierPtr &&
                 pointerKilledBefore(L, earlierPtr, store, queryStart,
                                     AnalysisTimeout)) {
          ++numKilledBackwardStore;
          R.insert(remedy);
          res = NoModRef;
        }


      }

    }

    if( res == Ref || res == NoModRef )
    {
      INTROSPECT(EXIT(i1,Rel,i2,L,res));
      queryStart = 0;
      return res;
    }

    // Forward kills:
    //  Can this operation write a value which may be read by a later iteration.
    //  We must check if there is a later operation in this loop
    //  which MUST define it, thus killing the flow.
    if( L->contains(earlier) )
    {
      if( const StoreInst *store = dyn_cast< StoreInst >(earlier) )
      {
        ++numEligibleForwardStoreQueries;
        //if( pointerKilledAfter(L, store->getPointerOperand(), store, queryStart, AnalysisTimeout) )
        if( pointerKilledAfter(L, earlierPtr, store, queryStart, AnalysisTimeout) )
        {
          ++numKilledForwardStoreFlows;
          //res = ModRefResult(res & ~Mod);
          R.insert(remedy);
          res = NoModRef;
          return res;
        }
      }


      CallSite cs = getCallSite(earlier);
      if( cs.getInstruction() )
        if( Function *f = cs.getCalledFunction() )
          if( !f->isDeclaration() )
          {
            ++numEligibleForwardCallQueries;

/* Evidence suggests that this does not improve AA performance,
 * but takes a long time:

            if( allLoadsAreKilledAfter(L, cs) )
            {
              ++numKilledForwardCallFlows;
              res = ModRefResult(res & ~Mod);
            }
*/
          }

      // handle WAR deps
      if( const LoadInst *load = dyn_cast< LoadInst >(earlier) )
      {
        ++numEligibleForwardLoadQueries;
        //if( pointerKilledAfter(L, load->getPointerOperand(), load, queryStart, AnalysisTimeout) )
        if( pointerKilledAfter(L, earlierPtr, load, queryStart, AnalysisTimeout) )
        {
          DEBUG(errs() << "Killed dep: load inst as earlier : " << *load << "\n later is "  << *later << "\n");
          ++numKilledForwardLoad;
          R.insert(remedy);
          res = NoModRef;
          return res;
        }
      }
    }


    DEBUG(errs() << "Can't say jack about " << *i2 << " at "
      << i2->getParent()->getParent()->getName() << ':' << i2->getParent()->getName() << '\n');
    INTROSPECT(EXIT(i1,Rel,i2,L,res));
    if (res != NoModRef) {
      Remedies tmpR;
      ModRefResult nextRes = getEffectiveNextAA()->modref(i1, Rel, i2, L, tmpR);
      if (ModRefResult(res & nextRes) != res) {
        for (auto remed : tmpR)
          R.insert(remed);
        res = ModRefResult(res & nextRes);
      }
    }
    return res;
  }

  bool KillFlow_CtrlSpecAware::instMustKillAggregate(const Instruction *inst, const Value *aggregate, time_t queryStart, unsigned Timeout)
  {
    // llvm.lifetime.start, llvm.lifetime.end are intended to limit
    // the lifetime of memory objects.  They are especially powerful
    // for alloca's that were inlined, since the alloca's can be moved
    // to the caller's header.  We model them as storing an undef
    // value to the memory location.
    if( const IntrinsicInst *intrinsic = dyn_cast<IntrinsicInst>(inst) )
    {
      if( intrinsic->getIntrinsicID() == Intrinsic::lifetime_start
      ||  intrinsic->getIntrinsicID() == Intrinsic::lifetime_end )
      {
        Value *lifeptr = intrinsic->getArgOperand(1);

        //sot
        const Module *M = inst->getModule();
        const DataLayout &DL = M->getDataLayout();

        // lifetime intrinsics are usually only applied to allocas;
        // don't do a full-on top query to compare.
        if( mustAliasFast(lifeptr, aggregate, DL) )
        {
          DEBUG(errs() << "Killed by " << *intrinsic << '\n');
          return true;
        }

        return false;
      }
    }

    CallSite cs = getCallSite(inst);
    if( cs.getInstruction() )
    {
      if( const MemIntrinsic *mi = dyn_cast< MemIntrinsic >(inst) )
      {
        const Value *killed = GetUnderlyingObject( mi->getRawDest(), *DL, 0 );
        if( mustAlias(killed, aggregate) )
        {
          // TODO did we kill the whole aggregate?
          return true;
        }

        return false;
      }

      if( Function *f = cs.getCalledFunction() )
      {
        if( f->isDeclaration() )
          return false;

        // basic blocks which post-dominate
        // the function entry.  These are the blocks which
        // MUST execute every time the function is invoked.
        const PostDominatorTree *pdt = getPDT(f);
        DomTreeNode *start = pdt->getNode( &f->front() );
        assert(start && "No postdomtree node for block?");
        for(DomTreeNode *n=start; n; n=n->getIDom() )
        {
          const BasicBlock *bb = n->getBlock();
          if( !bb )
            break;

          if( blockMustKillAggregate(bb, aggregate, 0, 0, queryStart, Timeout) )
            return true;
        }
      }
    }

    return false;
  }


  bool KillFlow_CtrlSpecAware::blockMustKillAggregate(const BasicBlock *bb, const Value *aggregate, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout)
  {
    const BasicBlock *afterbb  = after ? after->getParent() : 0;

    bool start = (afterbb != bb);
    for(BasicBlock::const_iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      const Instruction *inst = &*j;

      if( !start )
      {
        if( inst == after )
          start = true;
        continue;
      }

      if( inst == before )
        break;

      if( !inst->mayWriteToMemory() )
        continue;

      if( instMustKillAggregate(inst, aggregate, queryStart, Timeout) )
        return true;
    }

    return false;
  }

  /// Determine if there is an operation in <L> which must execute before <before> which kills <ptr>
  bool KillFlow_CtrlSpecAware::pointerKilledBefore(const Loop *L, const Value *ptr, const Instruction *before, bool alsoCheckAggregate, time_t queryStart, unsigned Timeout)
  {
    if (!L || !specDT || !specPDT || tgtLoop != L)
      return false;

//    INTROSPECT(errs() << "KillFlow_CtrlSpecAware: pointerKilledBefore(" << *before << "):\n");

    // Find those blocks which dominate the load and which
    // are contained within the loop.
    const BasicBlock *beforebb = before->getParent();

    const Function *f = beforebb->getParent();
    const DominatorTree *dt = getDT(f);
    DomTreeNode *start = dt->getNode( const_cast<BasicBlock*>( beforebb ));
    if( !start )
    {
      errs() << "The dominator tree does not contain basic block: " << beforebb->getName() << '\n'
             << "  (this probably means that the block is unreachable)\n";

      // Conservative answer
      return false;
    }

    ControlSpeculation::LoopBlock iter =
        ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(beforebb));

    std::unordered_set<const BasicBlock *> visited;
    while (!iter.isBeforeIteration()) {
      const BasicBlock *bb = iter.getBlock();
      // TODO: not sure why for some loops, this gets stuck within an inner loop
      // and needs to check for visited (dijistra was one of the benchmarks that
      // had this problem)
      // need to check LoopDominators for any potential bug
      if (!bb)
        break;
      if (visited.count(bb))
        break;
      visited.insert(bb);
      if (!L->contains(bb))
        break;

      if (blockMustKill(bb, ptr, 0, before, queryStart, Timeout, L))
        return true;

      if (Timeout > 0 && queryStart > 0) {
        time_t now;
        time(&now);
        if ((now - queryStart) > Timeout) {
          errs() << "KillFlow_CtrlSpecAwareAA::pointerKilledBefore Timeout\n";
          return false;
        }
      }

      if (bb == L->getHeader())
        break;

      ControlSpeculation::LoopBlock next = specDT->idom(iter);
      if (next == iter)
        // self-loop. avoid infinite loop
        break;

      iter = next;
    }

    /*

    for(DomTreeNode *n=start; n; n=n->getIDom() )
    {
      const BasicBlock *bb = n->getBlock();
      if( !bb )
        break;
      if( L && !L->contains(bb) )
        break;

//      INTROSPECT(errs() << "\to BB " << bb->getName() << '\n');

      if( blockMustKill(bb, ptr, 0, before, queryStart, Timeout, L) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::pointerKilledBefore Timeout\n";
          return false;
        }
      }
    }
    */

    // Not killed; maybe this is part of an aggregate!
    if( alsoCheckAggregate )
    {
      const Value *aggregate = GetUnderlyingObject(ptr, *DL, 0);
      if( !aggregate || aggregate == ptr )
        return false;

      if( aggregateKilledBefore(L,aggregate,before,queryStart,Timeout) )
        return true;
    }

    return false;
  }


  /// Determine if there is an operation in <L> which must execute
  /// after <after> and before <before> which kills <ptr>
  bool KillFlow_CtrlSpecAware::pointerKilledBetween(const Loop *L, const Value *ptr, const Instruction *after, const Instruction *before, bool alsoCheckAggregate, time_t queryStart, unsigned Timeout)
  {
    if (!L || !specDT || !specPDT || tgtLoop != L)
      return false;

    // Find those blocks which dominate the load and which
    // are contained within the loop.
    const BasicBlock *beforebb = before->getParent();
    const BasicBlock *afterbb = after->getParent();

    const Function *f = beforebb->getParent();
    const DominatorTree *dt = getDT(f);
    const PostDominatorTree *pdt = getPDT(f);
    DomTreeNode *start = dt->getNode( const_cast<BasicBlock*>( beforebb ));
    if( !start )
    {
      errs() << "The dominator tree does not contain basic block: " << beforebb->getName() << '\n'
             << "  (this probably means that the block is unreachable)\n";

      // Conservative answer
      return false;
    }

    ControlSpeculation::LoopBlock iter =
        ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(beforebb));

    std::unordered_set<const BasicBlock *> visited;
    while (!iter.isBeforeIteration()) {
      const BasicBlock *bb = iter.getBlock();
      // TODO: not sure why for some loops, this gets stuck within an inner loop
      // and needs to check for visited (dijistra was one of the benchmarks that
      // had this problem)
      // need to check LoopDominators for any potential bug
      if (!bb)
        break;
      if (visited.count(bb))
        break;
      visited.insert(bb);
      if (!L->contains(bb))
        break;

      ControlSpeculation::LoopBlock lbBB =
          ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(bb));

      ControlSpeculation::LoopBlock lbAfterBB =
          ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(afterbb));

      if (!specPDT->pdom(lbBB, lbAfterBB))
        continue;

      if( blockMustKill(bb, ptr, after, before, queryStart, Timeout, L) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::pointerKilledBetween Timeout\n";
          return false;
        }
      }

      if (bb == L->getHeader())
        break;

      ControlSpeculation::LoopBlock next = specDT->idom(iter);
      if (next == iter)
        // self-loop. avoid infinite loop
        break;

      iter = next;
    }

    /*
    for(DomTreeNode *n=start; n; n=n->getIDom() )
    {
      const BasicBlock *bb = n->getBlock();
      if( !bb )
        break;
      if( L && !L->contains(bb) )
        break;
      if( ! pdt->dominates(bb, afterbb) )
        continue;

      if( blockMustKill(bb, ptr, after, before, queryStart, Timeout, L) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::pointerKilledBetween Timeout\n";
          return false;
        }
      }
    }
    */

    // Not killed; maybe this is part of an aggregate!
    if( alsoCheckAggregate )
    {
      const Value *aggregate = GetUnderlyingObject(ptr, *DL, 0);
      if( !aggregate || aggregate == ptr )
        return false;

      if( aggregateKilledBetween(L,aggregate,after,before,queryStart, Timeout) )
        return true;
    }

    return false;
  }



  /// Determine if there is an operation in <L> which must execute before <before> which kills the aggregate
  bool KillFlow_CtrlSpecAware::aggregateKilledBefore(const Loop *L, const Value *obj, const Instruction *before, time_t queryStart, unsigned Timeout)
  {
    if (!L || !specDT || !specPDT || tgtLoop != L)
      return false;

    // Find those blocks which dominate the load and which
    // are contained within the loop.
    const BasicBlock *beforebb = before->getParent();
    const Function *f = beforebb->getParent();
    const DominatorTree *dt = getDT(f);
    DomTreeNode *start = dt->getNode( const_cast<BasicBlock*>( beforebb ));
    if( !start )
    {
      errs() << "The dominator tree does not contain basic block: " << beforebb->getName() << '\n'
             << "  (this probably means that the block is unreachable)\n";

      // Conservative answer
      return false;
    }

    ControlSpeculation::LoopBlock iter =
        ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(beforebb));

    std::unordered_set<const BasicBlock *> visited;
    while (!iter.isBeforeIteration()) {
      const BasicBlock *bb = iter.getBlock();
      // TODO: not sure why for some loops, this gets stuck within an inner loop
      // and needs to check for visited (dijistra was one of the benchmarks that
      // had this problem)
      // need to check LoopDominators for any potential bug
      if (!bb)
        break;
      if (visited.count(bb))
        break;
      visited.insert(bb);
      if (!L->contains(bb))
        break;

      if( blockMustKillAggregate(bb, obj, 0, before, queryStart, Timeout) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::aggregateKilledBefore Timeout\n";
          return false;
        }
      }

      if (bb == L->getHeader())
        break;

      ControlSpeculation::LoopBlock next = specDT->idom(iter);
      if (next == iter)
        // self-loop. avoid infinite loop
        break;

      iter = next;
    }

    /*
    for(DomTreeNode *n=start; n; n=n->getIDom() )
    {
      const BasicBlock *bb = n->getBlock();
      if( !bb )
        break;
      if( L && !L->contains(bb) )
        break;

      if( blockMustKillAggregate(bb, obj, 0, before, queryStart, Timeout) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::aggregateKilledBefore Timeout\n";
          return false;
        }
      }
    }
    */

    return false;
  }


 /// Determine if there is an operation in <L> which must execute after <after> which kills <ptr>
  bool KillFlow_CtrlSpecAware::pointerKilledAfter(const Loop *L, const Value *ptr, const Instruction *after, bool alsoCheckAggregate, time_t queryStart, unsigned Timeout)
  {
    if (!L || !specDT || !specPDT || tgtLoop != L)
      return false;

    // Find those blocks which post-dominate the store and which
    // are contained within the loop.
    const BasicBlock *afterbb = after->getParent();
    const Function *f = afterbb->getParent();
    const PostDominatorTree *pdt = getPDT(f);
    DomTreeNode *start = pdt->getNode( const_cast<BasicBlock*>( afterbb ));
    if( !start )
    {
      errs() << "The post-dominator tree does not contain basic block: " << afterbb->getName() << '\n'
             << "  (this probably means that the block is located in an infinite loop)\n";

      // Conservative answer
      return false;
    }

    ControlSpeculation::LoopBlock iter =
        ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(afterbb));

    std::unordered_set<const BasicBlock *> visited;
    while (!iter.isBeforeIteration()) {
      const BasicBlock *bb = iter.getBlock();
      // TODO: not sure why for some loops, this gets stuck within an inner loop
      // and needs to check for visited (dijistra was one of the benchmarks that
      // had this problem)
      // need to check LoopDominators for any potential bug
      if (!bb)
        break;
      if (visited.count(bb))
        break;
      visited.insert(bb);
      if (!L->contains(bb))
        break;

      if( blockMustKill(bb, ptr, after, 0, queryStart, Timeout, L) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::pointerKilledAfter Timeout\n";
          return false;
        }
      }

      if (bb == L->getHeader())
        break;

      ControlSpeculation::LoopBlock next = specPDT->ipdom(iter);
      if (next == iter)
        // self-loop. avoid infinite loop
        break;

      iter = next;
    }

    /*
    for(DomTreeNode *n=start; n; n=n->getIDom() )
    {
      const BasicBlock *bb = n->getBlock();
      if( !bb )
        break;
      if( L && !L->contains(bb) )
        break;

      if( blockMustKill(bb, ptr, after, 0, queryStart, Timeout, L) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::pointerKilledAfter Timeout\n";
          return false;
        }
      }
    }
    */

    if( alsoCheckAggregate )
    {
      const Value *aggregate = GetUnderlyingObject(ptr, *DL, 0);
      if( !aggregate || aggregate == ptr )
        return false;

      if( aggregateKilledAfter(L,aggregate,after,queryStart,Timeout) )
        return true;
    }

    return false;
  }

  /// Determine if there is an operation in <L> which must execute after <after> which kills the aggregate
  bool KillFlow_CtrlSpecAware::aggregateKilledAfter(const Loop *L, const Value *obj, const Instruction *after, time_t queryStart, unsigned Timeout)
  {
    if (!L || !specDT || !specPDT || tgtLoop != L)
      return false;

    // Find those blocks which post-dominate the store and which
    // are contained within the loop.
    const BasicBlock *afterbb = after->getParent();
    const Function *f = afterbb->getParent();
    const PostDominatorTree *pdt = getPDT(f);
    DomTreeNode *start = pdt->getNode( const_cast<BasicBlock*>( afterbb ));
    if( !start )
    {
      errs() << "The dominator tree does not contain basic block: " << afterbb->getName() << '\n'
             << "  (this probably means that the block is located in an infinite loop)\n";

      // Conservative answer
      return false;
    }

    ControlSpeculation::LoopBlock iter =
        ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(afterbb));

    std::unordered_set<const BasicBlock *> visited;
    while (!iter.isBeforeIteration()) {
      const BasicBlock *bb = iter.getBlock();
      // TODO: not sure why for some loops, this gets stuck within an inner loop
      // and needs to check for visited (dijistra was one of the benchmarks that
      // had this problem)
      // need to check LoopDominators for any potential bug
      if (!bb)
        break;
      if (visited.count(bb))
        break;
      visited.insert(bb);
      if (!L->contains(bb))
        break;

      if( blockMustKillAggregate(bb, obj, after, 0, queryStart, Timeout) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::aggregateKilledAfter Timeout\n";
          return false;
        }
      }

      if (bb == L->getHeader())
        break;

      ControlSpeculation::LoopBlock next = specPDT->ipdom(iter);
      if (next == iter)
        // self-loop. avoid infinite loop
        break;

      iter = next;
    }

    /*
    for(DomTreeNode *n=start; n; n=n->getIDom() )
    {
      const BasicBlock *bb = n->getBlock();
      if( !bb )
        break;
      if( L && !L->contains(bb) )
        break;

      if( blockMustKillAggregate(bb, obj, after, 0, queryStart, Timeout) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::aggregateKilledAfter Timeout\n";
          return false;
        }
      }
    }
    */

    return false;
  }

  /// Determine if there is an operation in <L> which must execute after <after>
  /// and before <before> which kills the aggregate
  bool KillFlow_CtrlSpecAware::aggregateKilledBetween(const Loop *L, const Value *obj, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout)
  {
    if (!L || !specDT || !specPDT || tgtLoop != L)
      return false;

    // Find those blocks which post-dominate the store and which
    // are contained within the loop.
    const BasicBlock *afterbb = after->getParent();
    const BasicBlock *beforebb = before->getParent();
    const Function *f = afterbb->getParent();
    const DominatorTree *dt = getDT(f);
    const PostDominatorTree *pdt = getPDT(f);
    DomTreeNode *start = pdt->getNode( const_cast<BasicBlock*>( afterbb ));
    if( !start )
    {
      errs() << "The post-dominator tree does not contain basic block: " << afterbb->getName() << '\n'
             << "  (this probably means that the block is located in an infinite loop)\n";

      // Conservative answer
      return false;
    }

    ControlSpeculation::LoopBlock iter =
        ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(afterbb));

    std::unordered_set<const BasicBlock *> visited;
    while (!iter.isBeforeIteration()) {
      const BasicBlock *bb = iter.getBlock();
      // TODO: not sure why for some loops, this gets stuck within an inner loop
      // and needs to check for visited (dijistra was one of the benchmarks that
      // had this problem)
      // need to check LoopDominators for any potential bug
      if (!bb)
        break;
      if (visited.count(bb))
        break;
      visited.insert(bb);
      if (!L->contains(bb))
        break;

      ControlSpeculation::LoopBlock lbBB =
          ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(bb));

      ControlSpeculation::LoopBlock lbBeforeBB =
          ControlSpeculation::LoopBlock(const_cast<BasicBlock *>(beforebb));

      if (!specDT->dom(lbBB, lbBeforeBB))
        continue;

      if( blockMustKillAggregate(bb, obj, after, before, queryStart, Timeout) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::aggregateKilledBetween Timeout\n";
          return false;
        }
      }

      if (bb == L->getHeader())
        break;

      ControlSpeculation::LoopBlock next = specPDT->ipdom(iter);
      if (next == iter)
        // self-loop. avoid infinite loop
        break;

      iter = next;
    }

    /*
    for(DomTreeNode *n=start; n; n=n->getIDom() )
    {
      const BasicBlock *bb = n->getBlock();
      if( !bb )
        break;
      if( L && !L->contains(bb) )
        break;
      if( !dt->dominates(bb,beforebb) )
        continue;

      if( blockMustKillAggregate(bb, obj, after, before, queryStart, Timeout) )
        return true;

      if(Timeout > 0 && queryStart > 0)
      {
        time_t now;
        time(&now);
        if( (now - queryStart) > Timeout )
        {
          errs() << "KillFlow_CtrlSpecAwareAA::aggregateKilledBetween Timeout\n";
          return false;
        }
      }
    }
    */

    return false;
  }



static RegisterPass<KillFlow_CtrlSpecAware>
X("kill-flow-ctrl-spec-aa", "Reasons about operations which kill data flow between loop iterations with ctrl spec awareness");
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char KillFlow_CtrlSpecAware::ID = 0;

}
