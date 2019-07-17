#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Instructions.h"

#include "liberty/Analysis/FindSource.h"
#include "liberty/Utilities/GepRange.h"
#include "liberty/Utilities/GlobalMalloc.h"

#include <unordered_set>

using namespace llvm;

const Value *liberty::getCanonicalGepRange(const GetElementPtrInst *gep,
                                           const Loop *L, ScalarEvolution *se) {
  if (!gep)
    return nullptr;

  // the first index of gep should be a SCEV that starts from 0 and goes
  // up to a limit with step 1
  unsigned numIndices = gep->getNumIndices();
  if (numIndices < 1)
    return nullptr;
  const Value *idx0 = *gep->idx_begin();

  if (!se->isSCEVable(idx0->getType()))
    return nullptr;
  const SCEVAddRecExpr *addRec =
      dyn_cast<SCEVAddRecExpr>(se->getSCEV(const_cast<Value *>(idx0)));

  if (!addRec)
    return nullptr;

  return getCanonicalRange(addRec, L, se);
}

const Value *liberty::getCanonicalRange(const SCEVAddRecExpr *addRec,
                                        const Loop *L, ScalarEvolution *se) {

  if (!addRec || !addRec->isAffine())
    return nullptr;

  if (addRec->getLoop() != L)
    return nullptr;

  const SCEV *start = addRec->getOperand(0);
  if (const SCEVConstant *startConst = dyn_cast<SCEVConstant>(start))
    if (startConst->getAPInt() != 0)
      return nullptr;

  const SCEV *step = addRec->getOperand(1);
  if (const SCEVConstant *stepConst = dyn_cast<SCEVConstant>(step))
    if (stepConst->getAPInt() != 1)
      return nullptr;

  if (!se->hasLoopInvariantBackedgeTakenCount(L))
    return nullptr;
  const SCEV *tripCount = se->getBackedgeTakenCount(L);
  auto *sMax = dyn_cast<SCEVSMaxExpr>(tripCount);
  if (!sMax)
    return nullptr;

  const SCEVUnknown *sMax1 = nullptr;
  if (auto *vcastKill = dyn_cast<SCEVCastExpr>(sMax->getOperand(1)))
    sMax1 = dyn_cast<SCEVUnknown>(vcastKill->getOperand());
  else
    sMax1 = dyn_cast<SCEVUnknown>(sMax->getOperand(1));

  if (!sMax1)
    return nullptr;

  const Value *limit = sMax1->getValue();

  return limit;
}

bool liberty::hasLoopInvariantTripCountUnknownSCEV(const Loop *L) {
  if (getLimitUnknown(nullptr, L))
    return true;
  return false;
}

const Value *liberty::getLimitUnknown(const Value *idx, const Loop *L) {
  const Instruction *idxI;
  if (idx) {
    idxI = dyn_cast<Instruction>(idx);
    if (!idxI)
      return nullptr;
  }

  const BasicBlock *header = L->getHeader();

  // check that the loop has only one exiting BB, the loop header
  if (!L->getExitingBlock() || header != L->getExitingBlock())
    return nullptr;

  // get compare operands in loop header branch
  const BranchInst *term = dyn_cast<BranchInst>(header->getTerminator());
  if (!term || !term->isConditional())
    return nullptr;
  const ICmpInst *cmp = dyn_cast<ICmpInst>(term->getCondition());
  if (!cmp)
    return nullptr;
  auto predicate = cmp->getUnsignedPredicate();
  if (predicate != ICmpInst::ICMP_ULT)
    return nullptr;
  const Value *limit = cmp->getOperand(1);
  const Value *IV = cmp->getOperand(0);

  // check if 'IV' is indeed a pseudo-canonical induction variable
  const Instruction *I = dyn_cast<Instruction>(IV);
  std::unordered_set<const Instruction *> visited;
  bool accumFound = false;
  bool phiFound = false;
  while (I && !visited.count(I) && L->contains(I)) {
    visited.insert(I);
    if (auto phi = dyn_cast<PHINode>(I)) {
      if (phi->getNumIncomingValues() == 1 &&
          L->contains(phi->getIncomingBlock(0))) {
        I = dyn_cast<Instruction>(phi->getIncomingValue(0));
        continue;
      }
      if (phiFound)
        return nullptr;
      phiFound = true;
      if (phi->getNumIncomingValues() != 2)
        return nullptr;
      if (phi->getParent() != header)
        return nullptr;
      const ConstantInt *start;
      if (!L->contains(phi->getIncomingBlock(1))) {
        I = dyn_cast<Instruction>(phi->getIncomingValue(0));
        start = dyn_cast<ConstantInt>(phi->getIncomingValue(1));
      } else if (!L->contains(phi->getIncomingBlock(0))) {
        I = dyn_cast<Instruction>(phi->getIncomingValue(1));
        start = dyn_cast<ConstantInt>(phi->getIncomingValue(0));
      } else
        return nullptr;
      if (!start)
        return nullptr;
      if (start->getSExtValue() != 0)
        return nullptr;
      continue;
    } else if (I->getOpcode() == Instruction::Add) {
      Value *Op1 = I->getOperand(0);
      Value *Op2 = I->getOperand(1);
      if (auto c1 = dyn_cast<ConstantInt>(Op1)) {
        I = dyn_cast<Instruction>(Op2);
        if (c1->getSExtValue() != 1 || accumFound)
          return nullptr;
        accumFound = true;
        continue;
      } else if (auto c2 = dyn_cast<ConstantInt>(Op2)) {
        I = dyn_cast<Instruction>(Op1);
        if (c2->getSExtValue() != 1 || accumFound)
          return nullptr;
        accumFound = true;
        continue;
      } else
        return nullptr;
    } else if (auto loadI = dyn_cast<LoadInst>(I)) {
      const GlobalValue *gv = liberty::findGlobalSource(loadI);
      if (!gv)
        return nullptr;
      std::vector<const Instruction *> srcs;
      bool noCaptureGV = findNoCaptureGlobalSrcs(gv, srcs);
      if (!noCaptureGV)
        return nullptr;
      if (srcs.size() != 1)
        return nullptr;
      auto srcI = *srcs.begin();
      if (srcI->getParent() != header || loadI->getParent() == header)
        return nullptr;
      auto storeI = dyn_cast<StoreInst>(srcI);
      I = dyn_cast<Instruction>(storeI->getValueOperand());
    } else
      return nullptr;
  }

  if (!phiFound || !accumFound)
    return nullptr;

  if (idx) {
    if (!visited.count(idxI))
      return nullptr;
  }

  return limit;
}

const Value *liberty::bypassExtInsts(const Value *v) {
  while (isa<SExtInst>(v) || isa<ZExtInst>(v)) {
    const Instruction *I = dyn_cast<Instruction>(v);
    v = I->getOperand(0);
  }
  return v;
}

