#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

const Value *getCanonicalGepRange(const GetElementPtrInst *gep, const Loop *L,
                                  ScalarEvolution *se) {
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
  if (!addRec->isAffine())
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
