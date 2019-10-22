#define DEBUG_TYPE   "remediator"

#include "llvm/IR/GlobalVariable.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GetMemOper.h"

namespace liberty
{
  using namespace llvm;

  STATISTIC(numPrivRead,    "Private reads instrumented");
  STATISTIC(numPrivWrite,   "Private writes instrumented");
  STATISTIC(numReduxWrite,  "Redux writes instrumented");

  Remedies Remediator::satisfy(const PDG &pdg, Loop *loop,
                               const Criticisms &criticisms) {
    Remedies remedies;
    for (Criticism *cr : criticisms) {
      Instruction *sop = dyn_cast<Instruction>(cr->getOutgoingT());
      Instruction *dop = dyn_cast<Instruction>(cr->getIncomingT());
      assert(sop && dop &&
             "PDG nodes that are part of criticims should be instructions");
      bool lc = cr->isLoopCarriedDependence();
      DataDepType dataDepTy;
      if (cr->isRAWDependence())
        dataDepTy = DataDepType::RAW;
      else if (cr->isWAWDependence())
        dataDepTy = DataDepType::WAW;
      else
        dataDepTy = DataDepType::WAR;
      Remedy_ptr r;
      if (cr->isMemoryDependence())
        r = tryRemoveMemEdge(sop, dop, lc, dataDepTy, loop);
      else if (cr->isControlDependence())
        r = tryRemoveCtrlEdge(sop, dop, lc, loop);
      else
        r = tryRemoveRegEdge(sop, dop, lc, loop);
      if (r) {
        // remedy found for this criticism
        auto it = remedies.find(r);
        if (it != remedies.end()) {
          // this remedy already satisfied previous criticim(s)
          (*it)->resolvedC.insert(cr);
        }
        else {
          r->resolvedC.insert(cr);
          remedies.insert(r);
        }
      }
    }
    return remedies;
  }

  Remedy_ptr Remediator::tryRemoveMemEdge(const Instruction *sop,
                                          const Instruction *dop, bool lc,
                                          DataDepType dataDepTy,
                                          const Loop *loop) {
    RemedResp remedResp = memdep(sop, dop, lc, dataDepTy, loop);
    if (remedResp.depRes == DepResult::NoDep)
      return remedResp.remedy;
    else
      return nullptr;
  }

  Remedy_ptr Remediator::tryRemoveRegEdge(const Instruction *sop,
                                          const Instruction *dop, bool lc,
                                          const Loop *loop) {
    RemedResp remedResp = regdep(sop, dop, lc, loop);
    if (remedResp.depRes == DepResult::NoDep)
      return remedResp.remedy;
    else
      return nullptr;
  }

  Remedy_ptr Remediator::tryRemoveCtrlEdge(const Instruction *sop,
                                           const Instruction *dop, bool lc,
                                           const Loop *loop) {
    RemedResp remedResp = ctrldep(sop, dop, loop);
    if (remedResp.depRes == DepResult::NoDep)
      return remedResp.remedy;
    else
      return nullptr;
  }

  // default conservative implementation of memdep,regdep,ctrldep

  Remediator::RemedResp Remediator::memdep(const Instruction *sop,
                                           const Instruction *dop, bool lc,
                                           DataDepType dataDepTy,
                                           const Loop *loop) {
    RemedResp remedResp;
    remedResp.depRes = DepResult::Dep;
    return remedResp;
  }

  Remediator::RemedResp Remediator::regdep(const Instruction *sop,
                                           const Instruction *dop, bool lc,
                                           const Loop *loop) {
    RemedResp remedResp;
    remedResp.depRes = DepResult::Dep;
    return remedResp;
  }

  Remediator::RemedResp Remediator::ctrldep(const Instruction *sop,
                                            const Instruction *dop,
                                            const Loop *loop) {
    RemedResp remedResp;
    remedResp.depRes = DepResult::Dep;
    return remedResp;
  }

  // meant for RAW/WAW. ignores WAR (always resolved by memVer for free (in
  // process based parallelization).
  static const Value *getPtrDepBased(const Instruction *I, bool rawDep,
                                     bool srcI) {
    const Value *ptr = liberty::getMemOper(I);
    // if ptr null, check for memcpy/memmove inst.
    // src pointer is read, dst pointer is written.
    // choose pointer for current query based on dataDepTy
    if (!ptr) {
      if (const MemTransferInst *mti = dyn_cast<MemTransferInst>(I)) {
        if (rawDep && !srcI)
          ptr = mti->getRawSource();
        else
          ptr = mti->getRawDest();
      }
    }
    return ptr;
  }

  bool Remediator::noMemoryDep(const Instruction *src, const Instruction *dst,
                               LoopAA::TemporalRelation FW,
                               LoopAA::TemporalRelation RV, const Loop *loop,
                               LoopAA *aa, bool rawDep, Remedies &R) {
    Remedies tmpR1, tmpR2, tmpR, aliasTmpR;

    const Value *ptrSrc = getPtrDepBased(src, rawDep, true);
    const Value *ptrDest = getPtrDepBased(dst, rawDep, false);
    LoopAA::ModRefResult aliasRes = LoopAA::ModRef;
    // similar to ClassicLoopAA functionality of lifting modref to alias but
    // with high-level knowledge of the type of dependence
    if (ptrSrc && ptrDest) {
      if (LoopAA::NoAlias == aa->alias(ptrSrc, LoopAA::UnknownSize, FW, ptrDest,
                                       LoopAA::UnknownSize, loop, aliasTmpR)) {
        aliasRes = LoopAA::NoModRef;
      } else {
        aliasTmpR.clear();
        if (LoopAA::NoAlias == aa->alias(ptrDest, LoopAA::UnknownSize, RV,
                                         ptrSrc, LoopAA::UnknownSize, loop,
                                         aliasTmpR)) {
          aliasRes = LoopAA::NoModRef;
        }
      }
    }

    // forward dep test
    LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop, tmpR1);
    if (LoopAA::NoModRef == forward) {
      for (auto remed : tmpR1)
        tmpR.insert(remed);
      ClassicLoopAA::modrefAvoidExpRemeds(R, LoopAA::NoModRef, tmpR, aliasRes,
                                          aliasTmpR);
      return true;
    }

    // forward is Mod, ModRef, or Ref

    // reverse dep test
    LoopAA::ModRefResult reverse = forward;

    if (src != dst)
      reverse = aa->modref(dst, RV, src, loop, tmpR2);

    if (LoopAA::NoModRef == reverse) {
      for (auto remed : tmpR2)
        tmpR.insert(remed);
      ClassicLoopAA::modrefAvoidExpRemeds(R, LoopAA::NoModRef, tmpR, aliasRes,
                                          aliasTmpR);
      return true;
    }

    if (LoopAA::Ref == forward && LoopAA::Ref == reverse) {
      for (auto remed : tmpR1)
        tmpR.insert(remed);
      for (auto remed : tmpR2)
        tmpR.insert(remed);
      ClassicLoopAA::modrefAvoidExpRemeds(R, LoopAA::NoModRef, tmpR, aliasRes,
                                          aliasTmpR);
      return true; // RaR dep; who cares.
    }

    // At this point, we know there is one or more of
    // a flow-, anti-, or output-dependence.

    bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);
    bool WAR = (forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);
    bool WAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);

    if (rawDep && !RAW) {
      for (auto remed : tmpR1)
        tmpR.insert(remed);
      for (auto remed : tmpR2)
        tmpR.insert(remed);
      ClassicLoopAA::modrefAvoidExpRemeds(R, LoopAA::NoModRef, tmpR, aliasRes,
                                          aliasTmpR);
      return true;
    }

    if (!rawDep && !WAR && !WAW) {
      for (auto remed : tmpR1)
        tmpR.insert(remed);
      for (auto remed : tmpR2)
        tmpR.insert(remed);
      ClassicLoopAA::modrefAvoidExpRemeds(R, LoopAA::NoModRef, tmpR, aliasRes,
                                          aliasTmpR);
      return true;
    }

    if (aliasRes == LoopAA::NoModRef) {
      for (auto remed : aliasTmpR)
        R.insert(remed);
      return true;
    }

    return false;
  }

  LoopAA::ModRefResult Remediator::modref_many(const Instruction *A,
                                               LoopAA::TemporalRelation rel,
                                               const Instruction *B,
                                               const Loop *L, Remedies &R) {

    const Value *ptrA = liberty::getMemOper(A);
    const Value *ptrA2 = nullptr;
    const Value *ptrB = liberty::getMemOper(B);
    const Value *ptrB2 = nullptr;

    bool dstA = !ptrA;
    if (!ptrA) {
      if (const MemSetInst *msi = dyn_cast<MemSetInst>(A)) {
        ptrA = msi->getDest();
      } else if (const MemTransferInst *mti = dyn_cast<MemTransferInst>(A)) {
        ptrA = mti->getDest();
        ptrA2 = mti->getSource();
      }
    }

    if (!ptrB) {
      if (const MemSetInst *msi = dyn_cast<MemSetInst>(B)) {
        ptrB = msi->getDest();
      } else if (const MemTransferInst *mti = dyn_cast<MemTransferInst>(B)) {
        ptrB = mti->getDest();
        ptrB2 = mti->getSource();
      }
    }

    LoopAA::LoopAA::ModRefResult result =
        modref_with_ptrs(A, ptrA, rel, B, ptrB, L, R);

    if (dstA && ptrA)
      result = LoopAA::ModRefResult((~LoopAA::Ref) & result);

    if (!ptrA || !ptrB)
      return result;

    if (ptrA2)
      result = LoopAA::ModRefResult(
          result |
          LoopAA::ModRefResult((~LoopAA::Mod) &
                               modref_with_ptrs(A, ptrA2, rel, B, ptrB, L, R)));
    if (ptrB2)
      result =
          (!dstA) ? LoopAA::ModRefResult(
                        result | modref_with_ptrs(A, ptrA, rel, B, ptrB2, L, R))
                  : LoopAA::ModRefResult(
                        result |
                        LoopAA::ModRefResult(
                            (~LoopAA::Ref) &
                            modref_with_ptrs(A, ptrA, rel, B, ptrB2, L, R)));
    if (ptrA2 && ptrB2)
      result = LoopAA::ModRefResult(
          result | LoopAA::ModRefResult(
                       (~LoopAA::Mod) &
                       modref_with_ptrs(A, ptrA2, rel, B, ptrB2, L, R)));
    return result;
  }

  /*
  HeapAssignment::Type Remedy::selectHeap(const Value *ptr, const Loop *loop)
  const
  {
    const Ctx *ctx = read->getCtx(loop);
    return selectHeap(ptr,ctx);
  }

  HeapAssignment::Type Remedy::selectHeap(const Value *ptr, const Ctx *ctx)
  const
  {
    Ptrs aus;
    if( !read->getUnderlyingAUs(ptr,ctx,aus) )
      return HeapAssignment::Unclassified;

    return asgn->classify(aus);
  }

  bool Remedy::isPrivate(Value *ptr)
  {
    return selectHeap(ptr,loop) == HeapAssignment::Private;
  }

  //bool Remedy::isRedux(Value *ptr)
  //{
  //  return selectHeap(ptr,loop) == HeapAssignment::Redux;
  //}

  void Remedy::insertPrivateWrite(Instruction *gravity, InstInsertPt where,
  Value *ptr, Value *sz)
  {
    ++numPrivWrite;

    // Maybe cast to void*
    Value *base = ptr;
    if( base->getType() != voidptr )
    {
      Instruction *cast = new BitCastInst(ptr, voidptr);
      where << cast;
      //preprocess.addToLPS(cast, gravity);
      base = cast;
    }

    // Maybe cast the length
    Value *len = sz;
    if( len->getType() != u32 )
    {
      Instruction *cast = new TruncInst(len,u32);
      where << cast;
      //preprocess.addToLPS(cast, gravity);
      len = cast;
    }

    Constant *writerange = Api(mod).getPrivateWriteRange();
    Value *actuals[] = { base, len };
    Instruction *validation = CallInst::Create(writerange,
  ArrayRef<Value*>(&actuals[0], &actuals[2]) ); where << validation;
    //preprocess.addToLPS(validation, gravity);
  }

  void Remedy::insertReduxWrite(Instruction *gravity, InstInsertPt where, Value
  *ptr, Value *sz)
  {
    ++numReduxWrite;

    // Maybe cast to void*
    Value *base = ptr;
    if( base->getType() != voidptr )
    {
      Instruction *cast = new BitCastInst(ptr, voidptr);
      where << cast;
      //addToLPS(cast,gravity);
      base = cast;
    }

    // Maybe cast the length
    Value *len = sz;
    if( len->getType() != u32 )
    {
      Instruction *cast = new TruncInst(len,u32);
      where << cast;
      //addToLPS(cast,gravity);
      len = cast;
    }

    Constant *writerange = Api(mod).getReduxWriteRange();
    Value *actuals[] = { base, len };
    Instruction *validation = CallInst::Create(writerange,
  ArrayRef<Value*>(&actuals[0], &actuals[2]) ); where << validation;
    //addToLPS(validation, gravity);
  }

  void Remedy::insertPrivateRead(Instruction *gravity, InstInsertPt where, Value
  *ptr, Value *sz)
  {
    ++numPrivRead;

    // Name
    Twine msg = "Privacy violation on pointer " + ptr->getName()
                    + " in " + where.getFunction()->getName()
                    + " :: " + where.getBlock()->getName();
    Constant *message = getStringLiteralExpression(*mod, msg.str());

    // Maybe cast to void*
    Value *base = ptr;
    if( base->getType() != voidptr )
    {
      Instruction *cast = new BitCastInst(ptr, voidptr);
      where << cast;
      //preprocess.addToLPS(cast, gravity);
      base = cast;
    }

    // Maybe cast the length
    Value *len = sz;
    if( len->getType() != u32 )
    {
      Instruction *cast = new TruncInst(len,u32);
      where << cast;
      //preprocess.addToLPS(cast, gravity);
      len = cast;
    }

    Constant *readrange = Api(mod).getPrivateReadRange();
    Value *actuals[] = { base, len, message };
    Instruction *validation = CallInst::Create(readrange,
  ArrayRef<Value*>(&actuals[0], &actuals[3]) );

    where << validation;
    //preprocess.addToLPS(validation, gravity);
  }


  bool Remedy::replacePrivateLoadsStore(Instruction *origI) {
    // get cloned inst in the parallelized code
    Instruction *inst = task->instructionClones[origI];

    bool modified = false;
    if (LoadInst *load = dyn_cast<LoadInst>(inst)) {
      Value *ptr = load->getPointerOperand();

      //if (!isPrivate(loop, ptr))
      //  continue;

      DEBUG(errs() << "Instrumenting private load: " << *load << '\n');

      PointerType *pty = cast<PointerType>(ptr->getType());
      Type *eltty = pty->getElementType();
      uint64_t size = DL->getTypeStoreSize(eltty);
      Value *sz = ConstantInt::get(u32, size);

      insertPrivateRead(load, InstInsertPt::Before(load), ptr, sz);
      modified = true;
    } else if (StoreInst *store = dyn_cast<StoreInst>(inst)) {
      Value *ptr = store->getPointerOperand();

      //if (!isPrivate(loop, ptr))
      //  continue;

      DEBUG(errs() << "Instrumenting private store: " << *store << '\n');

      PointerType *pty = cast<PointerType>(ptr->getType());
      Type *eltty = pty->getElementType();
      uint64_t size = DL->getTypeStoreSize(eltty);
      Value *sz = ConstantInt::get(u32, size);

      insertPrivateWrite(store, InstInsertPt::Before(store), ptr, sz);
      modified = true;
    } else if (MemTransferInst *mti = dyn_cast<MemTransferInst>(inst)) {
      Value *src = mti->getRawSource(), *dst = mti->getRawDest(),
            *sz = mti->getLength();

      bool psrc = isPrivate(src), pdst = isPrivate(dst);

      if (psrc) {
        DEBUG(errs() << "Instrumenting private source of mti: " << *mti <<
  '\n');

        insertPrivateRead(mti, InstInsertPt::Before(mti), src, sz);
        modified = true;
      }

      if (pdst) {
        DEBUG(errs() << "Instrumenting private dest of mti: " << *mti << '\n');

        insertPrivateWrite(mti, InstInsertPt::Before(mti), dst, sz);
        modified = true;
      }
    } else if (MemSetInst *msi = dyn_cast<MemSetInst>(inst)) {
      Value *ptr = msi->getRawDest(), *sz = msi->getLength();

      //if (!isPrivate(loop, ptr))
      //  continue;

      DEBUG(errs() << "Instrumenting private dest of memset: " << *msi << '\n');

      insertPrivateWrite(msi, InstInsertPt::Before(msi), ptr, sz);
      modified = true;
    }
    return modified;
  }

  //void Remedy::replaceReduxStore(Instruction *inst) {
  //  if (StoreInst *store = dyn_cast<StoreInst>(inst)) {
  bool Remedy::replaceReduxStore(StoreInst *origSt) {
    // get cloned inst in the parallelized code
    Instruction *inst = task->instructionClones[(Instruction *)origSt];
    StoreInst *store = dyn_cast<StoreInst>(inst);
    assert(store);

    Value *ptr = store->getPointerOperand();

    // if (!isRedux(loop, ptr))
    //  continue;

    DEBUG(errs() << "Instrumenting redux store: " << *store << '\n');

    PointerType *pty = cast<PointerType>(ptr->getType());
    Type *eltty = pty->getElementType();
    uint64_t size = DL->getTypeStoreSize(eltty);
    Value *sz = ConstantInt::get(u32, size);

    insertReduxWrite(store, InstInsertPt::Before(store), ptr, sz);
    return true;
  }
  */

} // namespace liberty
