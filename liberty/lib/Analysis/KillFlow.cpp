#define DEBUG_TYPE "kill-flow-aa"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Analysis/KillFlow.h"
#include "liberty/Analysis/Introspection.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"
#include "liberty/Utilities/ModuleLoops.h"

#include "AnalysisTimeout.h"
#include <ctime>
#include <cmath>

namespace liberty
{

using namespace llvm;

STATISTIC(numQueriesReceived,              "Num queries passed to KillFlow");

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

  const PostDominatorTree *KillFlow::getPDT(const Function *cf)
  {
    Function *f = const_cast< Function * >(cf);
    PostDominatorTree *pdt = & mloops->getAnalysis_PostDominatorTree(f);
    return pdt;
  }

  const DominatorTree *KillFlow::getDT(const Function *cf)
  {
    Function *f = const_cast< Function * >(cf);
    DominatorTree *dt = & mloops->getAnalysis_DominatorTree(f);
    return dt;
  }

  ScalarEvolution *KillFlow::getSE(const Function *cf)
  {
    Function *f = const_cast< Function * >(cf);
    ScalarEvolution *se = & mloops->getAnalysis_ScalarEvolution(f);
    return se;
  }

  LoopInfo *KillFlow::getLI(const Function *cf)
  {
    Function *f = const_cast< Function * >(cf);
    LoopInfo *li = & mloops->getAnalysis_LoopInfo(f);
    return li;
  }

  bool KillFlow::mustAlias(const Value *storeptr, const Value *loadptr)
  {
    // Very easy case
    if( storeptr == loadptr && isa< GlobalValue >(storeptr) )
      return true;

    LoopAA *top = getEffectiveTopAA();
    ++numSubQueries;
    return top->alias(storeptr,1, Same, loadptr,1, 0) == MustAlias;
  }

  /// Non-topping case of pointer comparison.
  bool KillFlow::mustAliasFast(const Value *storeptr, const Value *loadptr, const DataLayout &DL)
  {
    UO a, b;
    GetUnderlyingObjects(storeptr,a,DL);
    if( a.size() != 1 )
      return false;
    GetUnderlyingObjects(loadptr,b,DL);
    return a == b;
  }


  bool KillFlow::instMustKill(const Instruction *inst, const Value *ptr, time_t queryStart, unsigned Timeout, const Loop *L)
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

  void KillFlow::uponStackChange()
  {
    fcnKills.clear();
    bbKills.clear();
  }

  bool KillFlow::aliasBasePointer(const Value *gepptr, const Value *killgepptr,
                                  const Value **numElemAU, ScalarEvolution *se,
                                  const Loop *L) {
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
      bool noStoreInLoop = true;
      bool nonMallocStore = false;
      for (auto use = globalSrc->user_begin(); use != globalSrc->user_end();
           ++use) {
        if (const StoreInst *store = dyn_cast<StoreInst>(*use)) {
          if (L->contains(store)) {
            noStoreInLoop = false;
            break;
          }
        } else if (const BitCastOperator *bcOp =
                       dyn_cast<BitCastOperator>(*use)) {
          for (auto bUse = bcOp->user_begin(); bUse != bcOp->user_end();
               ++bUse) {
            if (const StoreInst *store = dyn_cast<StoreInst>(*bUse)) {
              if (L->contains(store)) {
                noStoreInLoop = false;
                break;
              }

              if (!nonMallocStore)
                continue;
              const Type *type = globalSrc->getType()->getElementType();
              if (!type->isPointerTy())
                continue;
              const Type *elemType = (dyn_cast<PointerType>(type))->getElementType();
              if (elemType != killgepptr->getType())
                continue;
              if (!elemType->isSized())
                continue;
              uint64_t elemSize = DL->getTypeSizeInBits(const_cast<Type*>(elemType));
              if (elemSize == 0)
                continue;

              // bits->bytes
              elemSize = elemSize / 8;

              const Instruction *src = liberty::findNoAliasSource(store, *tli);
              if (!src) {
                nonMallocStore = true;
                continue;
              }
              if (auto allocCall = dyn_cast<CallInst>(src)) {
                if (allocCall->getNumArgOperands() != 1)
                  continue;
                const Value *mallocArg = allocCall->getArgOperand(0);
                if (const Instruction *mallocSizeI =
                        dyn_cast<Instruction>(mallocArg)) {
                  if (auto binMallocSizeI = dyn_cast<BinaryOperator>(mallocSizeI)) {
                    const ConstantInt *sizeOfElemInMalloc = nullptr;
                    const Value* N = nullptr;
                    if (isa<ConstantInt>(binMallocSizeI->getOperand(0))) {
                      sizeOfElemInMalloc =
                          dyn_cast<ConstantInt>(binMallocSizeI->getOperand(0));
                      N = binMallocSizeI->getOperand(1);
                    } else if (isa<ConstantInt>(
                                   binMallocSizeI->getOperand(1))) {
                      sizeOfElemInMalloc =
                          dyn_cast<ConstantInt>(binMallocSizeI->getOperand(1));
                      N = binMallocSizeI->getOperand(0);
                    }
                    if (!sizeOfElemInMalloc)
                      continue;

                    uint64_t sizeOfElem;
                    if (mallocSizeI->getOpcode() == Instruction::Shl) {
                      sizeOfElem = pow(2, sizeOfElemInMalloc->getZExtValue());
                    } else if (mallocSizeI->getOpcode() == Instruction::Mul) {
                      sizeOfElem = sizeOfElemInMalloc->getZExtValue();
                    }

                    if (elemSize != sizeOfElem)
                      continue;

                    *numElemAU = N;
                  }
                }
                // TODO: handle also malloc with contant size allocation
              }
            } else if (!isa<LoadInst>(*bUse)) {
              *numElemAU = nullptr;
              return false;
            }
          }
        } else if (!isa<LoadInst>(*use)) {
          *numElemAU = nullptr;
          return false;
        }
      }
      if (!noStoreInLoop || nonMallocStore)
        *numElemAU = nullptr;
      return noStoreInLoop;
    }

    return false;
  }

  bool KillFlow::greaterThan(const SCEV *killTripCount, const SCEV *tripCount,
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
        bool noStoreInLoop = true;
        for (auto use = globalSrc->user_begin(); use != globalSrc->user_end();
             ++use) {
          if (const StoreInst *store = dyn_cast<StoreInst>(*use)) {
            if (L->contains(store)) {
              noStoreInLoop = false;
              break;
            }
          } else if (const BitCastOperator *bcOp =
                         dyn_cast<BitCastOperator>(*use)) {
            for (auto bUse = bcOp->user_begin(); bUse != bcOp->user_end();
                 ++bUse) {
              if (const StoreInst *store = dyn_cast<StoreInst>(*bUse)) {
                if (L->contains(store)) {
                  noStoreInLoop = false;
                  break;
                }
              } else if (!isa<LoadInst>(*bUse)) {
                return false;
              }
            }
          } else if (!isa<LoadInst>(*use)) {
            return false;
          }
        }
        return noStoreInLoop;
      }
    }
    return false;
  }

  // kill idx should be equal or include the other inst's idx
  // or it kills the whole AU (no need to check inst's idx)
  bool KillFlow::matchingIdx(const Value *idx, const Value *killidx,
                             const Value *numElemAU, ScalarEvolution *se,
                             const Loop *L) {
    if (idx == killidx)
      return true;

    if (isa<ConstantInt>(idx) && isa<ConstantInt>(killidx)) {
      const ConstantInt * ciIdx = dyn_cast<ConstantInt>(idx);
      const ConstantInt * ciKillIdx = dyn_cast<ConstantInt>(killidx);
      return (ciIdx->getSExtValue() == ciKillIdx->getSExtValue());
    }

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
      } else if (killAddRec && numElemAU) {
        // the dominated idx is not an add expr and we cannot compare directly.
        // Examine if kill ovewrites the whole allocation unit; if that's the
        // case then no need to check the dominated idx, it will be contained in
        // one of the indexes of the kill.
        if (!killAddRec->isAffine() ||
            !se->hasLoopInvariantBackedgeTakenCount(killAddRec->getLoop()))
          return false;


        const SCEV *killStart = killAddRec->getOperand(0);
        if (const SCEVConstant *killStartConst =
                dyn_cast<SCEVConstant>(killStart))
          if (killStartConst->getAPInt() != 0)
            return false;

        const SCEV *killStep = killAddRec->getOperand(1);
        if (const SCEVConstant *killStepConst =
                dyn_cast<SCEVConstant>(killStep))
          if (killStepConst->getAPInt() != 1)
            return false;

        const SCEV *killTripCount =
            se->getBackedgeTakenCount(killAddRec->getLoop());
        auto *killSMax = dyn_cast<SCEVSMaxExpr>(killTripCount);
        if (!killSMax)
          return false;

        const SCEVUnknown *killSMax1 = nullptr;
        if (auto *vcastKill = dyn_cast<SCEVCastExpr>(killSMax->getOperand(1)))
          killSMax1 = dyn_cast<SCEVUnknown>(vcastKill->getOperand());
        else
          killSMax1 = dyn_cast<SCEVUnknown>(killSMax->getOperand(1));

        if (!killSMax1)
          return false;

        const Value *killLimit = killSMax1->getValue();
        if (!killLimit)
          return false;

        // compare that with the number of elements in unique malloc src
        const GlobalValue *globalSrc = liberty::findGlobalSource(numElemAU);
        const GlobalValue *killGlobalSrc = liberty::findGlobalSource(killLimit);
        if (globalSrc != killGlobalSrc)
          return false;
        else if (globalSrc) {
          bool noMoreThanOneStore = true;
          bool storeFound = false;
          for (auto use = globalSrc->user_begin(); use != globalSrc->user_end();
               ++use) {
            if (isa<StoreInst>(*use)) {
              if (storeFound) {
                noMoreThanOneStore = false;
                break;
              } else
                storeFound = true;
            } else if (const BitCastOperator *bcOp =
                           dyn_cast<BitCastOperator>(*use)) {
              for (auto bUse = bcOp->user_begin(); bUse != bcOp->user_end();
                   ++bUse) {
                if (isa<StoreInst>(*bUse)) {
                  if (storeFound) {
                    noMoreThanOneStore = false;
                    break;
                  } else
                    storeFound = true;
                } else if (!isa<LoadInst>(*bUse)) {
                  return false;
                }
              }
            } else if (!isa<LoadInst>(*use)) {
              return false;
            }
          }
          return noMoreThanOneStore;
        }
      }
    }
    return false;
  }

  /// Determine if the block MUST KILL the specified pointer.
  /// If <after> belongs to this block and <after> is not null, only consider operations AFTER <after>
  /// If <after> belongs to this block and <before> is is not null, only consider operations BEFORE <before>
  bool KillFlow::blockMustKill(const BasicBlock *bb, const Value *ptr, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout, const Loop *L)
  {
    const BasicBlock *beforebb = before ? before->getParent() : 0;
    const BasicBlock *afterbb  = after  ? after->getParent() : 0;

    // We try to cache the results.
    // Cache results are only valid if we are going to consider
    // the whole block, i.e. <pt> is not in this basic block.

    BBPtrPair key(bb,ptr);
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

    if (bb != beforebb && bb != afterbb && isa<GetElementPtrInst>(ptr) && L) {
      LoopInfo *li = getLI(bb->getParent());

      if (li->isLoopHeader(bb)) {
        Loop *innerLoop = li->getLoopFor(bb);

        if ((after && innerLoop->contains(after)) ||
            (before && innerLoop->contains(before)))
          // do not update the bbKills cache is in this case
          return false;

        if (innerLoop->getSubLoops().empty()) {

          ScalarEvolution *se = getSE(bb->getParent());

          for (auto loopBB : innerLoop->getBlocks()) {
            for (BasicBlock::const_iterator k = loopBB->begin(),
                                            e = loopBB->end();
                 k != e; ++k) {
              const Instruction *loopInst = &*k;

              if (const StoreInst *store = dyn_cast<StoreInst>(loopInst)) {
                const Value *storeptr = store->getPointerOperand();

                if (const GetElementPtrInst *killgep = dyn_cast<GetElementPtrInst>(storeptr)) {

                  const Value *killgepptr = killgep->getPointerOperand();
                  unsigned killNumIndices = killgep->getNumIndices();

                  const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptr);
                  const Value *gepptr = gep->getPointerOperand();
                  unsigned numIndices = gep->getNumIndices();

                  if (killNumIndices != numIndices)
                    continue;

                  const Value *numElemAU = nullptr;
                  if (!aliasBasePointer(gepptr, killgepptr, &numElemAU, se, L))
                    continue;

                  bool iKill = true;
                  auto killidx = killgep->idx_begin();
                  for (auto idx = gep->idx_begin(); idx != gep->idx_end();
                       ++idx) {

                    if (!matchingIdx(*idx, *killidx, numElemAU, se, L)) {
                      iKill = false;
                      break;
                    }
                    ++killidx;
                  }

                  if (iKill) {
                    DEBUG(errs() << "\t(in inst " << *loopInst << ")\n");
                    bbKills[key] = true;

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
      }
    }

    if( bb != beforebb && bb != afterbb )
      bbKills[key] = false;

    return false;
  }


  std::set<Instruction *> instList;
  bool KillFlow::allLoadsAreKilledBefore(const Loop *L, CallSite &cs, time_t queryStart, unsigned Timeout)
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
            if(instList.count(inst)==0)
            {
              instList.insert(inst);
              if( allLoadsAreKilledBefore(L,cs2,queryStart,Timeout) )
              {
                instList.erase(inst);
                continue;
              }
              instList.erase(inst);
            }
          }

      DEBUG(errs() << "\tbut " << *inst << " ruins it for this callsite.\n");
      return false;
    }

    return true;
  }

  KillFlow::KillFlow() : ModulePass(ID), fcnKills(), bbKills(), mloops(0), effectiveNextAA(0), effectiveTopAA(0) {}

  KillFlow::~KillFlow() {}

  LoopAA::ModRefResult KillFlow::modref(const Instruction *i1,
                      LoopAA::TemporalRelation Rel,
                      const Instruction *i2,
                      const Loop *L)
  {
    INTROSPECT(ENTER(i1,Rel,i2,L));
    ++numQueriesReceived;

    ModRefResult res = getEffectiveNextAA()->modref(i1,Rel,i2,L);
    INTROSPECT(errs() << "lower in the stack reports res=" << res << '\n');
    if( res == Ref || res == NoModRef )
    {
      INTROSPECT(EXIT(i1,Rel,i2,L,res));
      return res;
    }

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
      return res;
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
          if( const StoreInst *store = dyn_cast< StoreInst >(i1) )
          {
            ++numEligibleForwardStoreQueries;
            if( pointerKilledBetween(L,store->getPointerOperand(),i1,i2) )
            {
              ++numKilledForwardStoreFlows;
              //res = ModRefResult(res & ~Mod);
              res = NoModRef;
            }
          }

          if( const LoadInst *load = dyn_cast< LoadInst >(i2) )
          {
            ++numEligibleBackwardLoadQueries;
            if( pointerKilledBetween(L,load->getPointerOperand(),i1,i2) )
            {
              ++numKilledBackwardLoadFlows;
              //res = ModRefResult(res & ~Mod);
              res = NoModRef;
            }
          }

          // handle WAR
          if( const LoadInst *load = dyn_cast< LoadInst >(i1) )
          {
            ++numEligibleForwardLoadQueries;
            if( pointerKilledBetween(L,load->getPointerOperand(),i1,i2) )
            {
              ++numKilledForwardLoad;
              res = NoModRef;
            }
          }

          // handle WAW
          if( const StoreInst *store = dyn_cast< StoreInst >(i2) )
          {
            ++numEligibleBackwardStoreQueries;
            if( pointerKilledBetween(L,store->getPointerOperand(),i1,i2) )
            {
              ++numKilledBackwardStore;
              res = NoModRef;
            }
          }
        }
      }
      INTROSPECT(EXIT(i1,Rel,i2,L,res));
      return res;
    }

    const Instruction *earlier = i1, *later = i2;
    if( Rel == After )
      std::swap(earlier,later);

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
        if( pointerKilledBefore(L, load->getPointerOperand(), load, queryStart, AnalysisTimeout) )
        {
          ++numKilledBackwardLoadFlows;
          DEBUG(errs() << "Removed the mod bit at AAA\n");
          //res = ModRefResult(res & ~Mod);
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
        if( pointerKilledBefore(L, store->getPointerOperand(), store, queryStart, AnalysisTimeout) )
        {
          ++numKilledBackwardStore;
          // no dependence between this store and insts from previous iteration possible
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
        if( pointerKilledAfter(L, store->getPointerOperand(), store, queryStart, AnalysisTimeout) )
        {
          ++numKilledForwardStoreFlows;
          //res = ModRefResult(res & ~Mod);
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
        if( pointerKilledAfter(L, load->getPointerOperand(), load, queryStart, AnalysisTimeout) )
        {
          DEBUG(errs() << "Killed dep: load inst as earlier : " << *load << "\n later is "  << *later << "\n");
          ++numKilledForwardLoad;
          res = NoModRef;
          return res;
        }
      }
    }


    DEBUG(errs() << "Can't say jack about " << *i2 << " at "
      << i2->getParent()->getParent()->getName() << ':' << i2->getParent()->getName() << '\n');
    INTROSPECT(EXIT(i1,Rel,i2,L,res));
    return res;
  }

  bool KillFlow::instMustKillAggregate(const Instruction *inst, const Value *aggregate, time_t queryStart, unsigned Timeout)
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


  bool KillFlow::blockMustKillAggregate(const BasicBlock *bb, const Value *aggregate, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout)
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
  bool KillFlow::pointerKilledBefore(const Loop *L, const Value *ptr, const Instruction *before, bool alsoCheckAggregate, time_t queryStart, unsigned Timeout)
  {
//    INTROSPECT(errs() << "KillFlow: pointerKilledBefore(" << *before << "):\n");

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
          errs() << "KillFlowAA::pointerKilledBefore Timeout\n";
          return false;
        }
      }
    }

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
  bool KillFlow::pointerKilledBetween(const Loop *L, const Value *ptr, const Instruction *after, const Instruction *before, bool alsoCheckAggregate, time_t queryStart, unsigned Timeout)
  {
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
          errs() << "KillFlowAA::pointerKilledBetween Timeout\n";
          return false;
        }
      }
    }

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
  bool KillFlow::aggregateKilledBefore(const Loop *L, const Value *obj, const Instruction *before, time_t queryStart, unsigned Timeout)
  {
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
          errs() << "KillFlowAA::aggregateKilledBefore Timeout\n";
          return false;
        }
      }
    }

    return false;
  }


 /// Determine if there is an operation in <L> which must execute after <after> which kills <ptr>
  bool KillFlow::pointerKilledAfter(const Loop *L, const Value *ptr, const Instruction *after, bool alsoCheckAggregate, time_t queryStart, unsigned Timeout)
  {
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
          errs() << "KillFlowAA::pointerKilledAfter Timeout\n";
          return false;
        }
      }
    }

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
  bool KillFlow::aggregateKilledAfter(const Loop *L, const Value *obj, const Instruction *after, time_t queryStart, unsigned Timeout)
  {
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
          errs() << "KillFlowAA::aggregateKilledAfter Timeout\n";
          return false;
        }
      }
    }

    return false;
  }

  /// Determine if there is an operation in <L> which must execute after <after>
  /// and before <before> which kills the aggregate
  bool KillFlow::aggregateKilledBetween(const Loop *L, const Value *obj, const Instruction *after, const Instruction *before, time_t queryStart, unsigned Timeout)
  {
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
          errs() << "KillFlowAA::aggregateKilledBetween Timeout\n";
          return false;
        }
      }
    }

    return false;
  }



static RegisterPass<KillFlow>
X("kill-flow-aa", "Reasons about operations which kill data flow between loop iterations.");
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char KillFlow::ID = 0;

}

