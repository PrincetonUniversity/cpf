#define DEBUG_TYPE "reduxdet"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Redux/ReductionDetection.h"

namespace liberty
{
  cl::opt<bool> FAST_MATH(
    "fast-math", cl::init(true), cl::NotHidden,
    //"fast-math", cl::init(false), cl::NotHidden,
    cl::desc("Fast math---allow transforms which may change floating point results slightly."));


using namespace llvm;

bool isDefUseForPHI(const PHINode *dst, const Instruction *addInst)
{
  for (unsigned x = 0; x < addInst->getNumOperands(); ++x)
  {
    Value *Op = addInst->getOperand(x);
    if (dyn_cast<Instruction>(Op) == dst)
      return true;
  }
  return false;
}

bool isAddInst(const Instruction *src)
{
  if (src->getOpcode() == Instruction::Add || src->getOpcode() == Instruction::FAdd )
    return true;

  if( FAST_MATH && src->getOpcode() == Instruction::FAdd)
    return true;

  return false;
}

Instruction* findAddInstDefForPHI(const PHINode *src, const Instruction *dst)
{
  Instruction *aInst = NULL;
  if (src->getNumIncomingValues() != 2)
    return NULL;

  Value *Op1 = src->getIncomingValue(0);
  Value *Op2 = src->getIncomingValue(1);
  Instruction *ii1 = dyn_cast<Instruction>(Op1);
  Instruction *ii2 = dyn_cast<Instruction>(Op2);

  if (ii1 && isAddInst(ii1) &&
      ii2 && ii2 == dst)
    aInst = ii1;
  else if (ii2 && isAddInst(ii2) &&
      ii1 && ii1 == dst)
    aInst = ii2;
  return aInst;
}

Instruction* getAddInstDefForPHI(const PHINode *src)
{
  if (src->getNumIncomingValues() != 1)
    return NULL;

  Value *Op1 = src->getIncomingValue(0);
  Instruction *ii1 = dyn_cast<Instruction>(Op1);

  if (ii1 && isAddInst(ii1))
    return ii1;

  return NULL;
}

bool ReductionDetection::isSumReduction(const Loop *loop,
                                        const Instruction *src,
                                        const Instruction *dst,
                                        const bool loopCarried,
                                        SpecPriv::Reduction::Type &type) {
  LLVM_DEBUG(errs() << "Testing PDG Edge for sum reduction: " << *src << " -> "
               << *dst << "\n");
  if (!src || !dst)
    return false;

  const PHINode *IndVar = loop->getCanonicalInductionVariable();
  if (IndVar && dst == IndVar)
    return false;

  // Handle two often seen patterns of SUM reduction
  // Pattern 1:
  // x0 = phi(initial from outside loop, x1 from backedge)
  // tmp = add x0, ...
  // x1 = phi(x0 from loop header, tmp from ...)
  const Instruction *addInst = NULL;
  bool isSumRedux = false;
  if (dst->getOpcode() == Instruction::PHI &&
      dst->getParent() == loop->getHeader() && loopCarried &&
      src->getOpcode() == Instruction::PHI &&
      (addInst = findAddInstDefForPHI(dyn_cast<PHINode>(src), dst)) &&
      (isDefUseForPHI(dyn_cast<PHINode>(dst), addInst)) && dst->hasOneUse() &&
      src->hasOneUse()) {
    LLVM_DEBUG(errs() << "\nSum Reduction:Found edge: " << *src << "\n            "
                 << *dst << "\naddInst: " << *addInst
                 << "\naccumValue: " << *dst << "\n");

    isSumRedux = true;
  }

  // Pattern 0:
  // x0 = phi(initial from outside loop, x1 from backedge)
  // tmp = add x0, ...
  // x1 = phi(tmp from ...)
  else if (dst->getOpcode() == Instruction::PHI &&
           dst->getParent() == loop->getHeader() && loopCarried &&
           src->getOpcode() == Instruction::PHI &&
           (addInst = getAddInstDefForPHI(dyn_cast<PHINode>(src))) &&
           (isDefUseForPHI(dyn_cast<PHINode>(dst), addInst)) &&
           dst->hasOneUse() && src->hasOneUse()) {
    LLVM_DEBUG(errs() << "\nSum Reduction:Found edge: " << *src << "\n            "
                 << *dst << "\naddInst: " << *addInst
                 << "\naccumValue: " << *dst << "\n");
    isSumRedux = true;
  }

  // Pattern 2:
  // x0 = phi(initial from outside loop, x1 from backedge)
  // x1 = add x0, ...
  else if (dst->getOpcode() == Instruction::PHI &&
           dst->getParent() == loop->getHeader() && loopCarried &&
           isAddInst(src) && (isDefUseForPHI(dyn_cast<PHINode>(dst), src)) &&
           dst->hasOneUse() && src->hasOneUse()) {
    LLVM_DEBUG(errs() << "\nSum Reduction:Found edge: " << *src << "\n            "
                 << *dst << "\naddInst: " << *src << "\naccumValue: " << *dst
                 << "\n");
    addInst = src;
    isSumRedux = true;
  }
  /*
  // only looking for loopcarried
  // Pattern 2:
  // x0 = phi(initial from outside loop, x1 from backedge)
  // x1 = add x0, ...
  else if (src->getOpcode() == Instruction::PHI &&
           src->getParent() == loop->getHeader() && loopCarried &&
           isAddInst(dst) && (isDefUseForPHI(dyn_cast<PHINode>(src), dst))) {
    LLVM_DEBUG(errs() << "\nSum Reduction:Found edge: " << *src << "\n            "
                 << *dst << "\naddInst: " << *src << "\naccumValue: " << *dst
                 << "\n");
    return true;
  }
  */

  // This pattern corresponds to a sum-reduction in an inner
  // loop, e.g.
  //
  //  loop {
  //    phi1 = phi 0, phi3 // dst
  //    loop {
  //      phi2 = phi phi1, x
  //      x = phi2 + z
  //    }
  //    phi3 = phi phi1,x // src
  //  }
  //
  // where <edge> corresponds to the data dependence
  // from phi3 to phi1.
  else if (dst->getOpcode() == Instruction::PHI &&
           src->getOpcode() == Instruction::PHI && loopCarried) {

    LLVM_DEBUG(errs() << "a " << *src << " to " << *dst << "\n");
    const PHINode *srcPhi = dyn_cast<PHINode>(src);

    Value *op = 0;
    if (srcPhi->getIncomingValue(0) == dst &&
        srcPhi->getNumIncomingValues() > 1)
      op = srcPhi->getIncomingValue(1);
    else if (srcPhi->getNumIncomingValues() == 1 ||
             srcPhi->getIncomingValue(1) == dst)
      op = srcPhi->getIncomingValue(0);

    if (op) {
      LLVM_DEBUG(errs() << "b\n");
      if (BinaryOperator *binop = dyn_cast<BinaryOperator>(op)) {
        LLVM_DEBUG(errs() << "c\n");
        if (binop->getOpcode() == Instruction::Add) {
          LLVM_DEBUG(errs() << "d\n");
          bool good = false;

          PHINode *phi2 = 0;
          if (0 != (phi2 = dyn_cast<PHINode>(binop->getOperand(0)))) {
            if (phi2->getIncomingValue(0) == dst ||
                (phi2->getNumIncomingValues() > 1 &&
                 phi2->getIncomingValue(1) == dst))
              good = true;
          } else if (0 != (phi2 = dyn_cast<PHINode>(binop->getOperand(1)))) {
            if (phi2->getIncomingValue(0) == dst ||
                (phi2->getNumIncomingValues() > 1 &&
                 phi2->getIncomingValue(1) == dst))
              good = true;
          }

          if (good) {
            LLVM_DEBUG(errs() << "Sum Reduction:Found edge: " << *src
                         << "\n            " << *dst << "\n"
                         << "PHI 1: " << *dst << '\n'
                         << "PHI 2: " << *phi2 << '\n'
                         << "ADD: " << *binop << '\n'
                         << "PHI 3: " << *src << '\n');
            return true;
          }
        }
      }
    }
  }

  if (isSumRedux && addInst) {
    const BinaryOperator *binop = dyn_cast<BinaryOperator>(addInst);
    if (binop)
      type = SpecPriv::Reduction::isAssocAndCommut(binop);
    return true;
  }

  LLVM_DEBUG(errs() << "\t- Finishing sum reduction check. Not found\n");
  return false;
}

// For branch-type min/max, src is the branch and dst is the phi.
// For select-type min/max, src is the cmp and dst is the select.
// Either way, check that src depends on dst, and dst has the same operands
// as the cmp.
bool findDependenceCycle(
    const Instruction *src, const Instruction *dst, bool &isFirstOperand,
    std::unordered_map<const Instruction *, MinMaxReductionInfo *>
        &minMaxReductions,
    Loop *loop) {

  // LLVM_DEBUG(errs() << "findDependenceCycle called: src: " << *src << "\n\tdst: "
  // << *dst << "\n");

  std::vector<const Instruction *> stack;
  stack.push_back(dst);

  DenseSet<const Instruction *> visited;

  const Instruction *cmpInst = NULL;

  while (!stack.empty()) {
    const Instruction *inst = stack.back();
    stack.pop_back();
    visited.insert(inst);

    if (inst == src) {
      // LLVM_DEBUG(errs() << "FOUND: " << *inst << '\n');
      assert(cmpInst && "Cycle should contain a CmpInst");

      // Check that operands of the cmp are the same as those of the sel/phi
      if (const SelectInst *sel = dyn_cast<SelectInst>(dst)) {
        if (cmpInst->getOperand(0) != sel->getTrueValue() &&
            cmpInst->getOperand(0) != sel->getFalseValue())
          return false;
        if (cmpInst->getOperand(1) != sel->getTrueValue() &&
            cmpInst->getOperand(1) != sel->getFalseValue())
          return false;
      } else {
        const PHINode *phi = cast<PHINode>(dst);
        // LLVM_DEBUG(errs() << "PHI: " << *phi << '\n');
        bool found0 = false, found1 = false;
        for (unsigned val = 0; val < phi->getNumIncomingValues(); ++val) {
          if (cmpInst->getOperand(0) == phi->getIncomingValue(val))
            found0 = true;
          if (cmpInst->getOperand(1) == phi->getIncomingValue(val))
            found1 = true;
        }
        if (!found0 || !found1)
          return false;
      }

      LLVM_DEBUG(errs() << "Dependence cycle found for " << *src << " , " << *dst
                   << "\n");
      errs() << "and the compare instruction is " << *cmpInst << "\n";
      return true;
    }

    if (!loop->contains(inst))
      continue;

    if (inst != dst && !isa<PHINode>(inst) && !isa<CmpInst>(inst)) {
      LLVM_DEBUG(errs() << "Shouldn't have a non-PHI/cmp use: " << *inst << "\n");
      return false;
  }

  for (Value::const_user_iterator U = inst->user_begin(), E = inst->user_end();
       U != E; ++U) {
    const Instruction *user = dyn_cast<Instruction>(*U);
    if (!user) {
      LLVM_DEBUG(errs() << "User is not an instruction\n");
      return false;
    }
    if (!visited.count(user)) {
      stack.push_back(user);

      if (isa<CmpInst>(user)) {
        if (cmpInst) {
          LLVM_DEBUG(errs() << "More than one compare inst in the cycle\n");
          return false; // there should only be one compare inst in the cycle
        }
        cmpInst = user;
        if (user->getOperand(0) == inst)
          isFirstOperand = true;
        else {
          assert(user->getOperand(1) == inst);
          isFirstOperand = false;
        }
      }
    }
  }
}

return false;
} // namespace liberty

Reduction::Type getDependentType(const Instruction *I,
                                 Reduction::Type depType) {
  if (depType == Reduction::Max_f32 || depType == Reduction::Max_f64 ||
      depType == Reduction::Max_i32 || depType == Reduction::Max_i64) {
    if (I->getType()->isIntegerTy(8))
      return Reduction::Max_i8;
    else if( I->getType()->isIntegerTy(16) )
      return Reduction::Max_i16;
    else if( I->getType()->isIntegerTy(32) )
      return Reduction::Max_i32;
    else if( I->getType()->isIntegerTy(64) )
      return Reduction::Max_i64;
    else if( I->getType()->isFloatTy() )
      return Reduction::Max_f32;
    else if( I->getType()->isDoubleTy() )
      return Reduction::Max_f64;
    else if( I->getType()->isPointerTy() )
      return Reduction::Max_u64;
    else {
      assert(0 && "Not yet implemented case for dependent type");
      return Reduction::NotReduction;
    }
  } else if (depType == Reduction::Min_f32 || depType == Reduction::Min_f64 ||
             depType == Reduction::Min_i32 || depType == Reduction::Min_i64) {
    if (I->getType()->isIntegerTy(8))
      return Reduction::Min_i8;
    else if (I->getType()->isIntegerTy(16))
      return Reduction::Min_i16;
    else if (I->getType()->isIntegerTy(32))
      return Reduction::Min_i32;
    else if (I->getType()->isIntegerTy(64))
      return Reduction::Min_i64;
    else if (I->getType()->isFloatTy())
      return Reduction::Min_f32;
    else if (I->getType()->isDoubleTy())
      return Reduction::Min_f64;
    else if( I->getType()->isPointerTy() )
      return Reduction::Min_u64;
    else {
      assert(0 && "Not yet implemented case for dependent type");
      return Reduction::NotReduction;
    }
  } else {
    assert(0 && "Not yet implemented case for dependent type");
    return Reduction::NotReduction;
  }
}

// This function should be called on all selects (could be extented for PHIs for
// branch min/max) in the same basic block as the dst of the reduction edge.
bool sameBBMinMaxRedux(
    const Instruction *dst, MinMaxReductionInfo *info,
    std::unordered_map<const Instruction *, MinMaxReductionInfo *>
        &minMaxReductions,
    Loop *loop, bool isMinMaxV) {

  //disabled this check to use this function for both select and branch
  //LLVM_DEBUG(errs() << "findDependenceCycle called: src: " << *src << "\n\tdst: "
  //<< *dst << "\n");
  //const SelectInst *sel = dyn_cast<SelectInst>(dst);
  //if (!sel || sel->getCondition() != info->cmpInst){
  //  LLVM_DEBUG(errs() << "sameBBMinMaxRedux return false because of non selectInst\n";)
  //  return false;
  //}

  std::vector<const Instruction *> stack;
  stack.push_back(dst);

  DenseSet<const Instruction *> visited;

  const Instruction *liveOutV = NULL;
  bool depCycleCompleted = false;

  while (!stack.empty()) {
    const Instruction *inst = stack.back();
    stack.pop_back();

    visited.insert(inst);

    if (!loop->contains(inst))
      continue;

    const CmpInst *ci = dyn_cast<CmpInst>(inst);
    if (ci && ci == info->cmpInst)
      continue;

    if (!isa<PHINode>(inst) && inst != dst ) {
      LLVM_DEBUG(errs() << "Shouldn't have a non-PHI use: " << *inst << "\n");
      return false;
    }

    for (Value::const_user_iterator U = inst->user_begin(), E = inst->user_end(); U != E;
         ++U) {
      const Instruction *user = dyn_cast<Instruction>(*U);
      if (!user) {
        LLVM_DEBUG(errs() << "User is not an instruction\n");
        return false;
      }
      if (!visited.count(user)) {
        stack.push_back(user);

        if (info && inst->getParent() == loop->getHeader())
          liveOutV = inst;
      } else if (user == dst)
        depCycleCompleted = true;
    }
  }

  if (liveOutV && depCycleCompleted) {
    LLVM_DEBUG(errs() << "MinMax redux for " << *liveOutV << "\n");

    if (isMinMaxV) {
      info->minMaxInst = liveOutV;
      info->type = SpecPriv::Reduction::isAssocAndCommut(info->cmpInst);
      info->depInst = nullptr;
      info->depType = Reduction::NotReduction;
      info->depUpdateInst = nullptr;
      minMaxReductions[liveOutV] = info;
    } else {
      MinMaxReductionInfo *newinfo = new MinMaxReductionInfo;
      newinfo->depInst = info->minMaxInst;
      newinfo->depType = info->type;
      newinfo->cmpInst = info->cmpInst;
      newinfo->type = getDependentType(liveOutV, newinfo->depType);
      const Instruction *depUpdateInst = dyn_cast<Instruction>(info->minMaxValue);
      assert(depUpdateInst);
      newinfo->depUpdateInst = depUpdateInst;
      minMaxReductions[liveOutV] = newinfo;
    }

    return true;
  }
  return false;
}

// Find a candidate edge.  This can be a control dependence edge from branch
// to phi, or a dataflow edge from compare to select.
MinMaxReductionInfo *
areCandidateInsts(const Instruction *src, const Instruction *dst,
                  const bool controlDep, PDG *pdg,
                  std::unordered_map<const Instruction *, MinMaxReductionInfo *>
                      &minMaxReductions,
                  Loop *loop) {

  if (!src || !dst)
    return NULL;

  // TODO: sot: if branch and phi case not sure it would be captured. select are
  // almost always used making it hard to test this case
  if (controlDep) {

    // 1. Check dst is move (PHI op)
    if (!isa<PHINode>(dst))
      return NULL;

    // 2. check comparison code
    const BranchInst *branchInst = dyn_cast<BranchInst>(src);
    if (!branchInst)
      return NULL;

    // Why do we sometimes see a control dep edge from an unconditional branch?
    if (branchInst->isUnconditional())
      return NULL;

    const CmpInst *cmpInst = dyn_cast<CmpInst>(branchInst->getCondition());
    if (!cmpInst)
      return NULL;

    if (const ICmpInst *icmpInst = dyn_cast<ICmpInst>(cmpInst)) {
      if (!icmpInst->isRelational())
        return NULL;
    } else if (const FCmpInst *fcmpInst = dyn_cast<FCmpInst>(cmpInst)) {
      if (!fcmpInst->isRelational())
        return NULL;
    } else
      return NULL;

    // LLVM_DEBUG(errs() << "CDEDGE: " << *src << "\n" << *dst << "\n");

    // 3. check def of dst is use of src
    // if (calTransitiveDependence(loopPDG, src, dst, cmpInst)) {
    bool isFirst;
    if (findDependenceCycle(src, dst, isFirst, minMaxReductions, loop)) {
      MinMaxReductionInfo *info = new MinMaxReductionInfo;
      info->cmpInst = cmpInst;
      info->minMaxValue = dst;
      info->isFirstOperand = isFirst;

      // to determine the sense of the compare, just check which side of the
      // branch goes directly to the block containing the phi node. this
      // assumes a certain control-flow structure; we could expand this for
      // more general control flow in the future.

      if (branchInst->getSuccessor(0) == dst->getParent()) {
        info->cmpTrueOnMinMax = false;
      } else if (branchInst->getSuccessor(1) == dst->getParent()) {
        info->cmpTrueOnMinMax = true;
      } else {
        // LLVM_DEBUG(errs() << "Could not determine sense of compare\n");
        delete info;
        return NULL;
      }

      LLVM_DEBUG(errs() << " cmpInst:     " << *info->cmpInst
                   << "\n minMaxValue: " << *info->minMaxValue
                   << "\n isFirstOperand: " << info->isFirstOperand
                   << "\n cmpTrueOnMinMax: " << info->cmpTrueOnMinMax << "\n");
      return info;
    } else {
      LLVM_DEBUG(errs() << "findDependenceCycle() returned false\n");
    }
  } else if (const SelectInst *sel = dyn_cast<SelectInst>(dst)) {

    if (!src)
      return NULL;

    if (const ICmpInst *icmpInst = dyn_cast<ICmpInst>(src)) {
      if (!icmpInst->isRelational())
        return NULL;
    } else if (const FCmpInst *fcmpInst = dyn_cast<FCmpInst>(src)) {
      if (!fcmpInst->isRelational())
        return NULL;
    } else
      return NULL;

    // check def of dst is use of src
    // if (calTransitiveDependence(loopPDG, src, dst, src)) {
    bool isFirst;
    if (findDependenceCycle(src, dst, isFirst, minMaxReductions, loop)) {
      const CmpInst *cmpInst = cast<CmpInst>(src);
      MinMaxReductionInfo *info = new MinMaxReductionInfo;
      info->cmpInst = cmpInst;
      info->minMaxValue = sel;
      info->isFirstOperand = isFirst;

      // this is kind of confusing. basically, when the select's TrueValue is
      // the same as the compare operand corresponding to the running
      // min/max, then cmpTrueOnMinMax is FALSE, because we are selecting the
      // old value.

      if ((isFirst && sel->getTrueValue() == cmpInst->getOperand(0)) ||
          (!isFirst && sel->getTrueValue() == cmpInst->getOperand(1)))
        info->cmpTrueOnMinMax = false;
      else
        info->cmpTrueOnMinMax = true;

      LLVM_DEBUG(errs() << " cmpInst:     " << *info->cmpInst
                   << "\n minMaxValue: " << *info->minMaxValue
                   << "\n isFirstOperand: " << info->isFirstOperand
                   << "\n cmpTrueOnMinMax: " << info->cmpTrueOnMinMax << "\n");
      return info;
    }
  }

  return NULL;
}

bool ReductionDetection::isMinMaxReduction(
    const Loop *loop, const Instruction *src, const Instruction *dst,
    const bool loopCarried, SpecPriv::Reduction::Type &type,
    const Instruction **depInst, SpecPriv::Reduction::Type &depType,
    const Instruction **depUpdateInst, const CmpInst **cmpInst) {
  LLVM_DEBUG(errs() << "Testing PDG Edge for min/max reduction: " << *src << " -> "
               << *dst << "\n";);
  if (loopCarried && (minMaxReductions.count(dst))) {
      LLVM_DEBUG(errs() << "dst is " << *dst << "\n");
    if(const Instruction* depInst = minMaxReductions[dst]->depInst){
      LLVM_DEBUG(errs() << "depInst is " << *depInst << "\n");
    }
    else{
      LLVM_DEBUG(errs() << "depInst is null\n");
    }
    type = minMaxReductions[dst]->type;
    *depInst = minMaxReductions[dst]->depInst;
    depType = minMaxReductions[dst]->depType;
    *depUpdateInst = minMaxReductions[dst]->depUpdateInst;
    *cmpInst = minMaxReductions[dst]->cmpInst;
    return true;
  }

  return false;
}

void ReductionDetection::findMinMaxRegReductions(Loop *loop, PDG *pdg) {
  minMaxReductions.clear();
  LLVM_DEBUG(errs() << "\t- Starting min/max reduction check.\n");
  for (auto edge : make_range(pdg->begin_edges(), pdg->end_edges())) {
    if (!pdg->isInternal(edge->getIncomingT()) ||
        !pdg->isInternal(edge->getOutgoingT()))
      continue;

    // Find an edge corresponding to a min/max reduction.
    //
    // TODO There is an issue with this code. If LLVM decides to
    // specialize a loop by creating a path where an inner loop is
    // executed and one path where it isn't, a reduction edge could
    // become cloned on either side. Then while exploring one side or the
    // other, the opposite side will prevent dependences from being removed
    // properly. As of June 5th, 2012 this is happening with KS if you inline
    // the CAiBj function.

    Instruction *src =
        dyn_cast<Instruction>(edge->getOutgoingT()); // src is br or cmp
    Instruction *dst = dyn_cast<Instruction>(edge->getIncomingT());

    if (MinMaxReductionInfo *info =
            areCandidateInsts(src, dst, edge->isControlDependence(), pdg,
                              minMaxReductions, loop)) {
      LLVM_DEBUG(errs() << "CandidateReduxInsts:"
                   << "\n      src = " << *src
                   << "\n      dst = " << *dst << "\n");

      // MIN/MAX Reduction Candidate!!
      bool infoUsed = false;
      infoUsed |= sameBBMinMaxRedux(dst, info, minMaxReductions, loop, true);
      // Detect edges corresponding to other reduction live-outs
      BasicBlock *bb = dst->getParent();
      for (BasicBlock::iterator bi = bb->begin(), ei = bb->end(); bi != ei;
           bi++) {
        Instruction *itmp = &(*bi);
        // Should only check moves (PHIs and selects).
        // NOTE: Should selects be more restrictive (i.e. only allowed if
        // original min/max used a select with the same condition?)
        // should exclude dst itself
        if ((isa<PHINode>(itmp) || isa<SelectInst>(itmp)) && itmp != src && itmp != dst) {
        //if (isa<SelectInst>(itmp) && itmp != src && itmp != dst) {
          infoUsed |=
              sameBBMinMaxRedux(itmp, info, minMaxReductions, loop, false);
        }
      }
      if (!infoUsed)
        delete info;
    }
  }
  LLVM_DEBUG(errs() << "\t- Finishing min/max reduction check.\n");
}

} // namespace liberty
