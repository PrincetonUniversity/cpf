#define DEBUG_TYPE "scalar-evolution-aa"

#include "llvm/Pass.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/Constants.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Utilities/ModuleLoops.h"

namespace liberty
{
using namespace llvm;

STATISTIC(numQueries,   "Num queries received");
STATISTIC(numEligible,  "Num eligible queries");
STATISTIC(numNoAlias,   "Num no-alias queries");
STATISTIC(numNoAliasMD, "Num no-alias queries with multi-dim check");
STATISTIC(numMustAlias, "Num must-alias queries");

/// This analysis compares SCEV expressions to analysis
/// induction variables.  It is meant to very quickly
/// handle a common case instead of using ModuleSMTAA
/// to handle a more general case slowly.
class SCEVAA : public ModulePass, public liberty::ClassicLoopAA {

public:
  static char ID;
  SCEVAA() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
  const DataLayout &DL = M.getDataLayout();
    InitializeLoopAA(this, DL);
    return false;
  }

  static bool alwaysGreaterThan(
    ScalarEvolution *SE,
    const SCEV *difference,
    const Loop *L,
    const APInt &positive, const APInt &negative)
  {
    const ConstantRange range = SE->getSignedRange(difference);

//    errs() << "alwaysGreaterThan( " << range << ", " << positive << ", " << negative << ")\n";

//    return   positive.ule( range.getUnsignedMin() )
//    &&    (-negative).uge( range.getUnsignedMax() );
    return positive.sle( range.getSignedMin() );
  }

  static bool stepGreaterThan(
    ScalarEvolution *SE,
    const Loop *L,
    const SCEV *ptr1, const APInt &size1, // (from earlier iteration)
    const SCEV *ptr2, const APInt &size2,  // (from later iteration)
		bool multiDimArrayEligible)
  {
/*
    The reasoning works like this:
          given p1=(base1, step1); p2=(base2, step2)
          let
            dbase = base2 - base1
            dstep = step2 - step1
              (note: dbase + i*dstep == p2i - p1 == distance between pointers at iteration i)
          if
            dbase >= 0
          and
            dstep >= 0
          and
            (dbase + i*dstep) + step2 >= size1
          then
            no-alias.

    Further, since k>=1 and dstep is non-negative, we can simplify:
      dbase >= 0 and dstep >= 0 and dbase + dstep + step2 >= size1 implies no-alias
*/

    // Deconstruct each stride into (base1,step1) and (base2,step2) w.r.t. loop L
    const unsigned BitWidth = SE->getTypeSizeInBits( ptr1->getType() );
    const APInt ap0(BitWidth, 0);
    const SCEV *zero = SE->getConstant(ap0);

    const SCEV *base1 = ptr1;
    const SCEV *step1 = zero;
    if( const SCEVAddRecExpr *ar1 = dyn_cast< SCEVAddRecExpr >(ptr1) )
      if( ar1->getLoop() == L )
      {
        base1 = ar1->getStart();
        step1 = ar1->getStepRecurrence(*SE);
      }

    const SCEV *base2 = ptr2;
    const SCEV *step2 = zero;
    if( const SCEVAddRecExpr *ar2 = dyn_cast< SCEVAddRecExpr >(ptr2) )
      if( ar2->getLoop() == L )
      {
        base2 = ar2->getStart();
        step2 = ar2->getStepRecurrence(*SE);
      }

    // At least one must stride
    if( step1 == zero && step2 == zero )
      return false;

//    errs() << "stepGreaterThan:\n"
//           << " earlier: " << size1 << " bytes from " << *base1 << " by " << *step1 << " byte increments\n"
//           << "   later: " << size2 << " bytes from " << *base2 << " by " << *step2 << " byte increments\n\n";

    // Consider the case where ptr2>ptr1:
    {
      const SCEV *diffBases = SE->getMinusSCEV( base2, base1 );
      const ConstantRange diffBasesRange = SE->getSignedRange(diffBases);

      const SCEV *diffStep = SE->getMinusSCEV(step2, step1);
      const ConstantRange diffStepRange = SE->getSignedRange(diffStep);

      // If the difference in bases is non-negative
//     	errs() << " forward difference in bases: " << diffBasesRange << ' ' << *diffBases << '\n';
      if( diffBasesRange.getSignedMin().sge(0) )
      {
        // and, If the difference in steps is non-negative
//        errs() << " forward difference in steps: " << diffStepRange << ' ' << *diffStep << '\n';
        if( diffStepRange.getSignedMin().sge(0) )
        {
          // and, if dbase+dstep >= size1
          const SCEV *minStep = SE->getAddExpr( diffBases, diffStep, step2 );
          const ConstantRange minStepRange = SE->getSignedRange(minStep);
//          errs() << " forward minimum step: " << minStepRange << ' ' << *minStep << '\n';

          if( minStepRange.getSignedMin().sge(size1) )
          {
//            errs() << "===> Disjoint\n";
            return true;
          }
        }
      }

      // sot: add extra check for pointers with same step and base (seems to
      // handle SCEVs with different subloops and semantically equivalent but
      // syntactically hard to process bases). Not applicable for inner most
      // loop accesses (useful for multi-dim array accesses)
      if (diffStepRange.getSignedMin() == 0 && multiDimArrayEligible) {

        const SCEVUnknown *ptrBase1 =
            dyn_cast<SCEVUnknown>(SE->getPointerBase(ptr1));
        if (!ptrBase1)
          return false;

        const SCEV *ptrSCEV = SE->getMinusSCEV(ptr1, ptrBase1);

        const SCEVAddRecExpr *sAR = dyn_cast<SCEVAddRecExpr>(ptrSCEV);

        if (sAR) {
          //const SCEV *base = sAR->getStart();
          const SCEV *step = sAR->getStepRecurrence(*SE);

          const SCEV *ElementSize = SE->getConstant(size1);
          SmallVector<const SCEV *, 4> Subscripts;
          SmallVector<const SCEV *, 4> Sizes;
          SE->delinearize(sAR, Subscripts, Sizes, ElementSize);

          if (Sizes.size() < 2)
            return false;

          // relevant size is the second to last size (the last one is equal to
          // the elementSize). the other sizes refer to outer loops if any
          unsigned relevantSizeIndex = Sizes.size() - 2;

          const SCEV *diffSCEV = SE->getMinusSCEV(
              step, SE->getMulExpr(ElementSize, Sizes[relevantSizeIndex]));
          const ConstantRange diffRange = SE->getSignedRange(diffSCEV);
          bool check = diffRange.getSignedMin().sge(0);

					if (check) {
              ++numNoAliasMD;
              DEBUG(errs() << "stepGreaterThan:\n"
                           << *ptr1 << " and " << *ptr2 << "\n===> Disjoint\n");
              return true;
          }
        }
      }
    }
    return false;
  }

  void delinearize(ScalarEvolution *SE, const Pointer &P, const APInt &size,
                   SmallVectorImpl<const SCEV *> &Sizes,
                   const SCEVUnknown *ptrBase) {
    const SCEV *pSCEV = SE->getSCEV(const_cast<Value *>(P.ptr));

    ptrBase = dyn_cast<SCEVUnknown>(SE->getPointerBase(pSCEV));
    if (ptrBase) {
      const SCEV *spSCEV = SE->getMinusSCEV(pSCEV, ptrBase);

      const SCEVAddRecExpr *sAR = dyn_cast<SCEVAddRecExpr>(spSCEV);

      if (sAR) {
        const SCEV *ElementSize = SE->getConstant(size);
        SmallVector<const SCEV *, 4> Subscripts;
        SE->delinearize(sAR, Subscripts, Sizes, ElementSize);
      }
    }
  }

  bool checkMultiDimArrayEligibility(const SCEVUnknown *ptrBase1,
                                     SmallVectorImpl<const SCEV *> &Sizes1,
                                     const SCEVUnknown *ptrBase2,
                                     SmallVectorImpl<const SCEV *> &Sizes2) {

    // check that both have the same pointer base
    if (!ptrBase1 && ptrBase1 != ptrBase2)
      return false;

    // check that both pointer access arrays of same dimensions
    if (Sizes1.size() != Sizes2.size() || Sizes1.size() < 2)
      return false;

    for (unsigned i = 0; i < Sizes1.size(); i++) {
      if (Sizes1[i] != Sizes2[i])
        return false;
    }

		return true;
  }

	static BasicBlock *GetBottom(DominatorTree &DT, const SCEV *S) {
		struct FindBottom {
			BasicBlock *Bottom = nullptr;
			DominatorTree &DT;

			FindBottom(DominatorTree &DT) : DT(DT) {}

			// Process a BB: if it is dominated by Bottom, it becomes the new Bottom.
			void CheckBB(BasicBlock *BB) {
				if (!Bottom) {
					Bottom = BB;
					return;
				}
				if (DT.dominates(Bottom, BB))
					Bottom = BB;
				else
					assert(DT.dominates(BB, Bottom) &&
								 "SCEV expressions always have a dominance relationship");
			}

			bool checkSCEVUnknown(const SCEVUnknown *SU) {
				if (auto *I = dyn_cast<Instruction>(SU->getValue()))
					CheckBB(I->getParent());
				return false;
			}

			bool checkSCEVAddRecExpr(const SCEVAddRecExpr *AddRec) {
				// (Note that we don't need to recuse into AddRecs: the operands
				// always dominate the loop.)
				CheckBB(AddRec->getLoop()->getHeader());
				return false;
			}

			bool follow(const SCEV *S) {
				switch (static_cast<SCEVTypes>(S->getSCEVType())) {
				case scConstant:
					return false;
				case scAddRecExpr:
					return checkSCEVAddRecExpr(cast<SCEVAddRecExpr>(S));
				case scTruncate:
				case scZeroExtend:
				case scSignExtend:
				case scAddExpr:
				case scMulExpr:
				case scUMaxExpr:
				case scSMaxExpr:
				case scUDivExpr:
					return true;
				case scUnknown:
					return checkSCEVUnknown(cast<SCEVUnknown>(S));
				case scCouldNotCompute:
					llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
				}
				return false;
			}
			bool isDone() { return false; }
		};
		FindBottom FB(DT);
		SCEVTraversal<FindBottom> ST(FB);
		ST.visitAll(S);
		return FB.Bottom;
	}

  static bool HasDominanceRelation(DominatorTree &DT, const SCEV *AS,
                                   const SCEV *BS) {
    BasicBlock *BottomA = GetBottom(DT, AS);
    BasicBlock *BottomB = GetBottom(DT, BS);
    return !BottomA || !BottomB || DT.dominates(BottomA, BottomB) ||
           DT.dominates(BottomB, BottomA);
  }

  virtual AliasResult aliasCheck(const Pointer &P1,
                                 TemporalRelation Rel,
                                 const Pointer &P2,
                                 const Loop *L)
  {
    ++numQueries;

    if( !L )
      return MayAlias;
    if( P1.size == 0 )
      return MayAlias;
    if( P2.size == 0 )
      return MayAlias;

    BasicBlock *header = L->getHeader();
    Function *fcn = header->getParent();

    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    ScalarEvolution *SE = & mloops.getAnalysis_ScalarEvolution(fcn);
    //ScalarEvolution *SE = &getAnalysis< ScalarEvolutionWrapperPass>(*fcn).getSE();
    DominatorTree &DT = mloops.getAnalysis_DominatorTree(fcn);

    if( !SE->isSCEVable( P1.ptr->getType() ) )
      return MayAlias;
    if( !SE->isSCEVable( P2.ptr->getType() ) )
      return MayAlias;

    const SCEV *s1 = SE->getSCEVAtScope( const_cast<Value*>(P1.ptr), L);
    if( !s1 )
      return MayAlias;
    const SCEV *s2 = SE->getSCEVAtScope( const_cast<Value*>(P2.ptr), L);
    if( !s2 )
      return MayAlias;

    const unsigned BitWidth = SE->getTypeSizeInBits( s1->getType() );
    APInt size1(BitWidth, P1.size);
    APInt size2(BitWidth, P2.size);

    if( Rel == LoopAA::Same )
    {
      if( s1 == s2 )
      {
        ++numMustAlias;
        return MustAlias; // true within one iteration; not necessarily across iterations.
      }
    }
    else
    {
      if( Rel == LoopAA::After )
        std::swap(s1,s2);
      // s1 is evaluated in an earlier iteration than s2.
    }

    if( SE->getEffectiveSCEVType(s1->getType()) != SE->getEffectiveSCEVType(s2->getType()) )
      return MayAlias;
    
    // fix dominance problem; may introduce more MayAlias
    if (Rel == LoopAA::Same && !HasDominanceRelation(DT, s1, s2))
      return MayAlias; 

    ++numEligible;

    //  We want to subtract these SCEVs to demonstrate that the difference
    //  in pointers is greater than the access size during any iteration.
    if ( Rel == LoopAA::Same )
    {
      //const BasicBlock* bb1 = NULL;
      //const BasicBlock* bb2 = NULL;

      //if (auto inst = dyn_cast<Instruction>(P1.ptr))
      //  bb1 = inst->getParent();
      //else
      //  if (auto arg = dyn_cast<Argument>(P1.ptr))
      //    bb1 = &(arg->getParent()->getEntryBlock());

      //if (auto inst = dyn_cast<Instruction>(P2.ptr))
      //  bb2 = inst->getParent();
      //else
      //  if (auto arg = dyn_cast<Argument>(P2.ptr))
      //    bb2 = &(arg->getParent()->getEntryBlock());

      //if (bb1 && bb2 && !DT.dominates(bb1, bb2) && !DT.dominates(bb2, bb1)){
      //    //DEBUG(
      //      errs() << "P1 and P2 not dominate\n";
      //    //);
      //    return MayAlias;
      //}
      /*
      else{
        DEBUG(
            if (!P1.inst)
              errs() << "P1 is not an instruction \n";
            if (!P2.inst)
              errs() << "P2 is not an instruction \n";
        );
      }
      */

      /*
      const bool s2IsNotMinSigned = !SE->getSignedRangeMin(s2).isMinSignedValue();
      auto NegFlags = s2IsNotMinSigned ? SCEV::FlagNSW : SCEV::FlagAnyWrap;
      auto negS2 = SE->getNegativeSCEV(s2, NegFlags);

      unsigned LType = s1->getSCEVType(), RType = negS2->getSCEVType();

      errs() << "s1 is " << *s1 << "  and \n s2 is " << *negS2 << '\n';
      errs() << "s1 is " << LType << "  and \n s2 is " << RType << '\n';

      if (static_cast<SCEVTypes>(LType) == scAddRecExpr && static_cast<SCEVTypes>(RType) == scAddRecExpr) {
        errs() << "it is scAddRecExpr\n";

          const SCEVAddRecExpr *addRecS1 = cast<SCEVAddRecExpr>(s1);
          const SCEVAddRecExpr *addRecS2 = cast<SCEVAddRecExpr>(negS2);
          if (addRecS1 && addRecS2)
            errs() << "add recurrences\n";

          DominatorTree &DT = mloops.getAnalysis_DominatorTree(fcn);

          const Loop *LLoop = addRecS1->getLoop(), *RLoop = addRecS2->getLoop();
               if (LLoop != RLoop) {
          const BasicBlock *LHead = LLoop->getHeader(), *RHead = RLoop->getHeader();
          if (!DT.dominates(LHead, RHead) && !DT.dominates(RHead, LHead))
            return MayAlias;
          }
      }
      */
     
      const SCEV *diff = SE->getMinusSCEV(s1,s2);
      if( alwaysGreaterThan(SE, diff, L,  size2, size1) )
      {
        ++numNoAlias;
        return NoAlias;
      }

      // Try the same in reverse
      diff = SE->getMinusSCEV(s2,s1);
      if( alwaysGreaterThan(SE, diff, L, size1, size2) )
      {
        ++numNoAlias;
        return NoAlias;
      }
    }
    else
    {
      // We want to subtract these SCEVs to demonstrate that the difference
      // in pointers must be greater than the access size during any
      // two iterations I1 < I2.

      bool innerMostLoopAccess =
          (s1 == SE->getSCEV(const_cast<Value *>(P1.ptr)) &&
           s2 == SE->getSCEV(const_cast<Value *>(P2.ptr)));

      const SCEVUnknown *ptrBase1;
      SmallVector<const SCEV *, 4> Sizes1;
      const SCEVUnknown *ptrBase2;
      SmallVector<const SCEV *, 4> Sizes2;
      delinearize(SE, P1, size1, Sizes1, ptrBase1);
      delinearize(SE, P2, size2, Sizes2, ptrBase2);

      bool multiDimArrayEligible =
          !innerMostLoopAccess &&
          checkMultiDimArrayEligibility(ptrBase1, Sizes1, ptrBase2, Sizes2);

      if( stepGreaterThan(SE, L, s1, size1, s2, size2, multiDimArrayEligible))
      {
        ++numNoAlias;
        return NoAlias;
      }
    }

    // These are the most interesting: eligible queries
    // for which we can't say anything.
    DEBUG(errs()
      << "Eligible fallthrough:\n"
      << "  size " << size1 << " scev " << *s1 << '\n'
      << "(" << Rel << ", " << fcn->getName() << " :: " << header->getName() << ")\n"
      << "  size " << size2 << " scev " << *s2 << '\n'
    );

    return MayAlias;
  }

  StringRef getLoopAAName() const {
    return "scev-loopaa";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired< ModuleLoops >();
    //AU.addRequired< ScalarEvolutionWrapperPass >();
    AU.setPreservesAll();                         // Does not transform code
  }

  /// getAdjustedAnalysisPointer - This method is used when a pass implements
  /// an analysis interface through multiple inheritance.  If needed, it
  /// should override this to adjust the this pointer as needed for the
  /// specified pass info.
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA*)this;
    return this;
  }
};

static RegisterPass<SCEVAA> X("scev-loop-aa", "Reasons about induction variables");
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char SCEVAA::ID = 0;
}
