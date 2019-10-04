#define DEBUG_TYPE "array-of-structures-aa"

#include "llvm/Pass.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/Constants.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/NoCaptureFcn.h"
#include "liberty/Analysis/TraceData.h"
#include "liberty/Utilities/ModuleLoops.h"

#include "NoEscapeFieldsAA.h"

namespace liberty
{
using namespace llvm;

STATISTIC(numEligible, "Num eligible");
STATISTIC(numNoAlias,  "Num no-alias/no-modref");

class ArrayOfStructures : public ModulePass, public liberty::ClassicLoopAA {

public:
  static char ID;
  ArrayOfStructures() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
    const DataLayout &DL = M.getDataLayout();
    InitializeLoopAA(this, DL);
    return false;
  }

  // Returns true iff v is not defined within a subloop of L.
  // Said another way, that means that any two observations
  // of 'v' within the same iteration of L must have the
  // same value.
  bool notDefinedWithinSubloop(const Value *v, const Loop *L) const
  {
    const Instruction *inst = dyn_cast< Instruction >(v);
    if( !inst )
      return true;

    if( inst->getParent()->getParent() != L->getHeader()->getParent() )
      return false; // cannot tell if L may invoke inst's parent

    if( !L->contains(inst) )
      return true;  // loop live-in

    for(Loop::iterator i=L->begin(), e=L->end(); i!=e; ++i)
    {
      const Loop *subloop = *i;
      if( subloop->contains(inst) )
        return false;
    }

    return true;
  }

  bool areStaticallyIdentical(Value *a, Value *b, const LoopAA::TemporalRelation Rel, const Loop *L, Tracer &tracer) const
  {
    // When same iteration of a loop, statically identical may also mean that they are
    // the same register temporary, and that register temporary is not
    // defined within a subloop of L.
    if( Rel == Same && L && a == b && notDefinedWithinSubloop(a,L) )
      return true;

    // Try to trace the values to find a unique integer value.
    Tracer::IntSet vals_a;
    if( tracer.traceConcreteIntegerValues(a, vals_a) && vals_a.size() == 1 )
    {
      Tracer::IntSet vals_b;
      if( tracer.traceConcreteIntegerValues(b, vals_b) )
        if( vals_a == vals_b )
          return true;
    }

    return false;
  }

  bool areStaticallyDifferent(Value *a, Value *b, const LoopAA::TemporalRelation Rel, const Loop *L, Tracer &tracer)
  {
    // When different iterations of the loop, statically different also means induction variable.
    if( Rel != LoopAA::Same && L && a == b )
    {
      const PHINode *civ = L->getCanonicalInductionVariable();
      if( civ && a == civ )
        return true;

      // Maybe it's not a /canonical/ induction variable, but it's
      // still an induction variable.  Check ScalarEvolution to find out.
      BasicBlock *header = L->getHeader();
      Function *fcn = header->getParent();

      ModuleLoops &mloops = getAnalysis< ModuleLoops >();
      ScalarEvolution &scev = mloops.getAnalysis_ScalarEvolution(fcn);
      //ScalarEvolution &scev = getAnalysis< ScalarEvolutionWrapperPass>(*fcn).getSE();
      if( scev.isSCEVable( a->getType() ) )
        if( const SCEV *ss = scev.getSCEVAtScope(a,L) )
          if( const SCEVAddRecExpr *induc = dyn_cast< SCEVAddRecExpr >(ss) )
            if( induc->getLoop() == L )
            {
              const SCEV *step = induc->getStepRecurrence(scev);
              if( scev.isKnownNonZero(step) )
                return true;
            }
    }

    // Try to trace the values
    Tracer::IntSet vals_a;
    if( tracer.traceConcreteIntegerValues(a, vals_a) )
    {
      Tracer::IntSet vals_b;
      if( tracer.traceConcreteIntegerValues(b, vals_b) )
        if( Tracer::disjoint(vals_a, vals_b) )
          return true;
    }

    return false;
  }

  virtual AliasResult aliasCheck(const Pointer &P1, TemporalRelation Rel,
                                 const Pointer &P2, const Loop *L,
                                 Remedies &R) {

    // We are looking for this pattern:
    //  0.  Two GEPs which MUST alias
    //  1.  Zero or more indices s.t. either,
    //      (a) The indices are statically identical, or
    //      (b) The indices may be different, but only at a GEP-array level.
    //  2.  One (statically) different index, at a GEP level which is a struct.

    // We will check 0 last, since it is a TOP operation, and could take a lot of time.
    // DO IT LAST.

    const Value *v1 = P1.ptr,
                *v2 = P2.ptr;

    const GEPOperator *gep1 = dyn_cast< GEPOperator >( v1 ),
                      *gep2 = dyn_cast< GEPOperator >( v2 );

    // handle cases where the gep is bitcasted before the mem operation
    auto bitcast1 = dyn_cast<BitCastInst>(v1);
    auto bitcast2 = dyn_cast<BitCastInst>(v2);

    if (bitcast1) {
      const Value *src1 = bitcast1->getOperand(0);
      if (const GEPOperator *srcGep1 = dyn_cast<GEPOperator>(src1))
        gep1 = srcGep1;
    }

    if (bitcast2) {
      const Value *src2 = bitcast2->getOperand(0);
      if (const GEPOperator *srcGep2 = dyn_cast<GEPOperator>(src2))
        gep2 = srcGep2;
    }

    // do not handle bitcasts to different types
    if ((bitcast1 || bitcast2) && (v1->getType() != v2->getType())) {
      return MayAlias;
    }

    if( !gep1 || !gep2 )
      return MayAlias;

    if( gep1->getPointerOperandType() != gep2->getPointerOperandType() )
      return MayAlias;

    ++numEligible;

    NoCaptureFcn &nocap = getAnalysis< NoCaptureFcn >();
    NonCapturedFieldsAnalysis &noescape = getAnalysis< NonCapturedFieldsAnalysis >();
    Tracer tracer(nocap,noescape);

    LLVMContext &ctx = gep1->getType()->getContext();

    // extra case (a variant of cond 1): check that base pointers must-alias,
    // base pointer's type is a pointer to a struct (array of structs) and
    // there is a statically different field index (skip the array level idx,
    // aka first index of the gep)
    const PointerType *gepPtrOpType =
        dyn_cast<PointerType>(gep1->getPointerOperandType());
    if (gepPtrOpType && gepPtrOpType->getElementType()->isStructTy() &&
        gep1->getNumIndices() > 1 && gep2->getNumIndices() > 1) {

      bool staticallyDiffIndexFound = false;
      User::const_op_iterator ix1 = gep1->idx_begin() + 1, e1 = gep1->idx_end(),
                              ix2 = gep2->idx_begin() + 1, e2 = gep2->idx_end();
      while (ix1 != e1 && ix2 != e2) {
        Value *cv1 = *ix1, *cv2 = *ix2;

        if (areStaticallyDifferent(cv1, cv2, Rel, L, tracer)) {
          staticallyDiffIndexFound = true;
          break;
        }

        ++ix1;
        ++ix2;
      }

      if (staticallyDiffIndexFound) {
        // 0. Check if the base pointers must alias.
        if (getTopAA()->alias(gep1->getPointerOperand(), 1, Same,
                              gep2->getPointerOperand(), 1, 0,
                              R) == MustAlias) {
          ++numNoAlias;
          return NoAlias;
        }
      }
    }

    gep_type_iterator gi1 = gep_type_begin(gep1),
                      gi2 = gep_type_begin(gep2);

    // 1. Zero or more indices, which are either
    //  - statically identical indices, or
    //  - different elements at an array level.
    User::const_op_iterator ix1 = gep1->op_begin() + 1,
                             e1 = gep1->op_end(),
                            ix2 = gep2->op_begin() + 1,
                             e2 = gep2->op_end();
    while( ix1 != e1 && ix2 != e2 )
    {
      Value *cv1 = *ix1,
            *cv2 = *ix2;

      //sot : operator* is no longer supported in LLVM 5.0 for gep_type_iterator
      // replaced with getIndexedType for Sequential Type and getStructTypeOrNull for Structs
      StructType *ST1 = gi1.getStructTypeOrNull();
      StructType *ST2 = gi2.getStructTypeOrNull();
      if (ST1 != ST2)
        return MayAlias;

      /*
      Type *t1 = gi1.getIndexedType();
      if( t1 != gi2.getIndexedType())
        return MayAlias;
      */

      // Heejin's fix in 3.5 seems incomplete
      //if( isa< SequentialType >(t1) || isa< PointerType >(t1)
      //||  areStaticallyIdentical(cv1, cv2, Rel, L, tracer) )
      if (areStaticallyIdentical(cv1, cv2, Rel, L, tracer))
      {
        ++ix1;
        ++ix2;
        ++gi1;
        ++gi2;
        continue;
      }

      else
        break;
    }

    // 2. One statically different index, at a GEP level which is a struct/array/pointer
    // i.e. we can prove that we're talking about two different fields/elements.

    // Iterators point to the first differing index.

    // Value of the differing index
    Value *cv1 = 0, *cv2 = 0;

    // If there are more indices
    if( ix1 != e1 )
      cv1 = *ix1;
    if( ix2 != e2 )
      cv2 = *ix2;

    // Implicit zero rule
    ConstantInt *zero = ConstantInt::get( Type::getInt64Ty(ctx) ,0);
    if( !cv1 && ix2 != e2 && ++ix2 == e2 ) // if second gep index at penultimate value
      cv1 = zero; // then first gets an implicit zero
    if( !cv2 && ix1 != e1 && ++ix1 == e1 ) // first first gep index at penultimate value
      cv2 = zero; // the second gets an implicit zero

    if( !cv1 || !cv2 )
      return MayAlias;

    //sot : operator* is no longer supported in LLVM 5.0 for gep_type_iterator
    // replaced with getIndexedType for Sequential Type and getStructTypeOrNull for Structs
    StructType *ST1 = gi1.getStructTypeOrNull();
    StructType *ST2 = gi2.getStructTypeOrNull();
    if (ST1 != ST2)
      return MayAlias;

    // sot: getIndexedType seems not to return the same result as *operator in
    // LLVM 3.5 (e.g., instead of getting a PointerType, it seeems that
    // PointeeTy is returned)
    /*
    Type *ty1 = gi1.getIndexedType();
    if( ty1 != gi2.getIndexedType() )
      return MayAlias;

    //if( !isa< CompositeType >(ty1) )
    if( !isa< CompositeType >(ty1) && !isa< PointerType > (ty1))
      return MayAlias;
    */

    if( ! areStaticallyDifferent(cv1,cv2,Rel,L,tracer) )
      return MayAlias;

    // 0. Check if the base pointers must alias.
    if (getTopAA()->alias(gep1->getPointerOperand(), 1, Same,
                          gep2->getPointerOperand(), 1, 0, R) == MustAlias) {
      ++numNoAlias;
      return NoAlias;
    }

    DEBUG(errs() << "Last minute failure " << *v1 << " vs " << *v2 << "\n");
    return MayAlias;
  }

  StringRef getLoopAAName() const {
    return "array-of-structures-aa";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired< ModuleLoops >();
    //AU.addRequired< ScalarEvolutionWrapperPass >();
    AU.addRequired< NoCaptureFcn >();
    AU.addRequired< NonCapturedFieldsAnalysis >();
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

static RegisterPass<ArrayOfStructures>
X("array-of-structures-aa", "Reasons about arrays of structures");
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char ArrayOfStructures::ID = 0;
}
