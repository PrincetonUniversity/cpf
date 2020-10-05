#define DEBUG_TYPE   "remediator"

#include "llvm/IR/GlobalVariable.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/Remediator.h"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/GetMemOper.h"
#include "liberty/Utilities/ReportDump.h"

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
                               LoopAA *aa, bool rawDep, bool wawDep,
                               Remedies &R) {

    // collect all different ways to remove the mem dep
    // either with alias query, or with fwd/reverse modref query in isolation or
    // with a combination of fwd and reverse responses.
    // At the end pick the cheapest of all.
    //
    Remedies aliasRemeds, fwdRemeds, reverseRemeds, fwdReverseRemeds;

    LoopAA::ModRefResult aliasRes = LoopAA::ModRef;
    LoopAA::ModRefResult fwdRes = LoopAA::ModRef;
    LoopAA::ModRefResult reverseRes = LoopAA::ModRef;
    LoopAA::ModRefResult fwdReverseRes = LoopAA::ModRef;

    const Value *ptrSrc = getPtrDepBased(src, rawDep, true);
    const Value *ptrDest = getPtrDepBased(dst, rawDep, false);
    // similar to ClassicLoopAA functionality of lifting modref to alias but
    // with high-level knowledge of the type of dependence
    if (ptrSrc && ptrDest) {
      if (LoopAA::NoAlias == aa->alias(ptrSrc, LoopAA::UnknownSize, FW, ptrDest,
                                       LoopAA::UnknownSize, loop, aliasRemeds,
                                       LoopAA::DNoAlias)) {
        aliasRes = LoopAA::NoModRef;
      } else {
        aliasRemeds.clear();
        if (LoopAA::NoAlias == aa->alias(ptrDest, LoopAA::UnknownSize, RV,
                                         ptrSrc, LoopAA::UnknownSize, loop,
                                         aliasRemeds, LoopAA::DNoAlias)) {
          aliasRes = LoopAA::NoModRef;
        }
      }
    }

    // forward dep test
    LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop, fwdRemeds);

    if (LoopAA::NoModRef == forward)
      fwdRes = LoopAA::NoModRef;

    // reverse dep test
    LoopAA::ModRefResult reverse = forward;

    if (FW != RV || src != dst) {
      reverse = aa->modref(dst, RV, src, loop, reverseRemeds);

      if (LoopAA::NoModRef == reverse)
        reverseRes = LoopAA::NoModRef;
    }

    // combine fwd and reverse

    bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);
    bool WAR = (forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);
    bool WAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
               (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);

    bool RAR = (LoopAA::Ref == forward && LoopAA::Ref == reverse);
    bool warDep = !rawDep && !wawDep;
    assert(!(rawDep && wawDep) && "Queries should be either RAW or WAW, not both!");

    if (RAR || (rawDep && !RAW) || (wawDep && !WAW) || (warDep && !WAR)) {
      LoopAA::appendRemedies(fwdReverseRemeds, fwdRemeds);
      LoopAA::appendRemedies(fwdReverseRemeds, reverseRemeds);
      fwdReverseRes = LoopAA::NoModRef;
    }

    // join all results and determine cheapest one
    Remedies tmpR1, tmpR2, finalRemeds;
    LoopAA::ModRefResult tmpRes1 = LoopAA::join(tmpR1, aliasRes, aliasRemeds, fwdRes, fwdRemeds);
    LoopAA::ModRefResult tmpRes2 = LoopAA::join(tmpR2, tmpRes1, tmpR1, reverseRes, reverseRemeds);
    LoopAA::ModRefResult finalRes = LoopAA::join(finalRemeds, tmpRes2, tmpR2, fwdReverseRes, fwdReverseRemeds);

    if (finalRes == LoopAA::NoModRef) {
      LoopAA::appendRemedies(R, finalRemeds);
      return true;
    }

    return false;
  }

  double
  Remediator::estimate_validation_weight(PerformanceEstimator *perf,
                                         const Instruction *gravity,
                                         unsigned long validation_weight) {

    const unsigned long relative_weight =
        perf->weight_with_gravity(gravity, validation_weight);

    return perf->convert_relative_weight(gravity, relative_weight);
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

    Remedies tmpR1;
    LoopAA::LoopAA::ModRefResult result =
        modref_with_ptrs(A, ptrA, rel, B, ptrB, L, tmpR1);

    if (dstA && ptrA) {
      if (result != LoopAA::Mod && result != LoopAA::ModRef) {
        for (auto remed : tmpR1)
          R.insert(remed);
      }
      result = LoopAA::ModRefResult((~LoopAA::Ref) & result);
    } else {
      if (result != LoopAA::ModRef) {
        for (auto remed : tmpR1)
          R.insert(remed);
      }
    }

    if (!ptrA || !ptrB)
      return result;


    if (ptrA2) {
      Remedies tmpR2;
      auto result2 = modref_with_ptrs(A, ptrA2, rel, B, ptrB, L, tmpR2);
      if (result2 != LoopAA::Ref && result2 != LoopAA::ModRef) {
        for (auto remed : tmpR2)
          R.insert(remed);
      }
      result2 = LoopAA::ModRefResult((~LoopAA::Mod) & result2);
      result = LoopAA::ModRefResult(result | result2);
    }
    if (ptrB2) {
      Remedies tmpR2;
      auto result2 = modref_with_ptrs(A, ptrA, rel, B, ptrB2, L, tmpR2);
      if (((result2 != LoopAA::Mod && dstA) || !dstA) &&
          result2 != LoopAA::ModRef) {
        for (auto remed : tmpR2)
          R.insert(remed);
      }
      result =
          (!dstA)
              ? LoopAA::ModRefResult(result | result2)
              : LoopAA::ModRefResult(
                    result | (LoopAA::ModRefResult((~LoopAA::Ref) & result2)));
    }
    if (ptrA2 && ptrB2) {
      Remedies tmpR2;
      auto result2 = modref_with_ptrs(A, ptrA2, rel, B, ptrB2, L, tmpR2);
      if (result2 != LoopAA::Ref && result2 != LoopAA::ModRef) {
        for (auto remed : tmpR2)
          R.insert(remed);
      }
      result2 = LoopAA::ModRefResult((~LoopAA::Mod) & result2);
      result = LoopAA::ModRefResult(result | result2);
    }
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

      REPORT_DUMP(errs() << "Instrumenting private load: " << *load << '\n');

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

      REPORT_DUMP(errs() << "Instrumenting private store: " << *store << '\n');

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
        REPORT_DUMP(errs() << "Instrumenting private source of mti: " << *mti <<
  '\n');

        insertPrivateRead(mti, InstInsertPt::Before(mti), src, sz);
        modified = true;
      }

      if (pdst) {
        REPORT_DUMP(errs() << "Instrumenting private dest of mti: " << *mti << '\n');

        insertPrivateWrite(mti, InstInsertPt::Before(mti), dst, sz);
        modified = true;
      }
    } else if (MemSetInst *msi = dyn_cast<MemSetInst>(inst)) {
      Value *ptr = msi->getRawDest(), *sz = msi->getLength();

      //if (!isPrivate(loop, ptr))
      //  continue;

      REPORT_DUMP(errs() << "Instrumenting private dest of memset: " << *msi << '\n');

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

    REPORT_DUMP(errs() << "Instrumenting redux store: " << *store << '\n');

    PointerType *pty = cast<PointerType>(ptr->getType());
    Type *eltty = pty->getElementType();
    uint64_t size = DL->getTypeStoreSize(eltty);
    Value *sz = ConstantInt::get(u32, size);

    insertReduxWrite(store, InstInsertPt::Before(store), ptr, sz);
    return true;
  }
  */

} // namespace liberty
