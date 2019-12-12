#define DEBUG_TYPE      "loopaa"

#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Analysis/LoopAA.h"

#include <cstdio>

namespace liberty
{
  using namespace llvm;

  char LoopAA::ID = 0;
  char NoLoopAA::ID = 0;
  char AAToLoopAA::ID = 0;
  char EvalLoopAA::ID = 0;

  cl::opt<bool> FULL_UNIVERSAL("full-universal",
                               cl::init(true), cl::Hidden,
                               cl::desc("Assume full visibility"));

  namespace
  {
    RegisterAnalysisGroup< LoopAA > loopaa("Loop-sensitive Alias Analysis");

    static RegisterPass<NoLoopAA>
    A("no-loop-aa",
      "No loop alias analysis (always return may-alias)",
      true, true);

    static RegisterAnalysisGroup<LoopAA, true> X(A);

    // Default: -basic-loop-aa
    static RegisterPass<AAToLoopAA>
    B("aa-to-loop-aa",
      "Basic loop AA (chain's to llvm::AliasAnalysis)",
      false, true);

    static RegisterAnalysisGroup<LoopAA> Y(B);

    static RegisterPass<EvalLoopAA> C("eval-loop-aa",
      "Exhaustive evaluation of loop AA", false, false);
  }

  // If enabled, this will make ::getTopAA() return the next AA in the stack;
  // In other words, it disables topping of precondition queries, and instead
  // chains them to the next query in the stack.
  //
  // This is an evil hack.  It only exists for evaluation in PLDI'14, and should
  // generally never be used.
  cl::opt<bool> EvilDisableTopHack(
    "evil-loopaa-disable-top", cl::init(false), cl::Hidden,
    cl::desc("(evil) Disable Topping in the LoopAA Stack, chaining instead"));

//------------------------------------------------------------------------
// Methods of the LoopAA interface

  LoopAA::LoopAA() : td(0), tli(0), nextAA(0), prevAA(0) {}

  LoopAA::~LoopAA()
  {
    if( nextAA )
      nextAA->prevAA = this->prevAA;
    if( prevAA )
      prevAA->nextAA = this->nextAA;

    getRealTopAA()->stackHasChanged();
  }

  void LoopAA::InitializeLoopAA(Pass *P, const DataLayout &t)
  {
    TargetLibraryInfoWrapperPass *tliWrap = &P->getAnalysis< TargetLibraryInfoWrapperPass>();
    TargetLibraryInfo *ti = &tliWrap->getTLI();
    LoopAA *naa = P->getAnalysis< LoopAA >().getRealTopAA();

    InitializeLoopAA(&t,ti,naa);

    getRealTopAA()->stackHasChanged();
  }

  void LoopAA::InitializeLoopAA(const DataLayout *t, TargetLibraryInfo *ti, LoopAA *naa)
  {
    td = t;
    tli = ti;

    // Don't insert this pass into the linked list twice
    if(prevAA || nextAA)
      return;

    // Insertion-sort this pass into the LoopAA stack.
    prevAA = 0;
    nextAA = naa;
    while( nextAA && nextAA->getSchedulingPreference() > this->getSchedulingPreference() )
    {
      prevAA = nextAA;
      nextAA = nextAA->nextAA;
    }

    if( prevAA )
      prevAA->nextAA = this;
    if( nextAA )
      nextAA->prevAA = this;

    assert(prevAA != this);
    assert(nextAA != this);

    LLVM_DEBUG( getRealTopAA()->print(errs()) );
  }

  // find top of stack.
  LoopAA *LoopAA::getRealTopAA() const
  {
    // The stack is short, so this won't take long.
    LoopAA *top = const_cast<LoopAA *>(this);
    while( top->prevAA )
      top = top->prevAA;
    return top;
  }

  LoopAA *LoopAA::getTopAA() const
  {
    if( EvilDisableTopHack )
      return nextAA;
    else
      return getRealTopAA();
  }

  void LoopAA::getAnalysisUsage( AnalysisUsage &au ) const
  {
    au.addRequired< TargetLibraryInfoWrapperPass >();
    au.addRequired< LoopAA >(); // all chain.
  }

  const DataLayout *LoopAA::getDataLayout() const
  {
    assert(td && "Did you forget to run InitializeLoopAA()?");
    return td;
  }

  const TargetLibraryInfo *LoopAA::getTargetLibraryInfo() const
  {
    assert(tli && "Did you forget to run InitializeLoopAA()?");
    return tli;
  }

  LoopAA::SchedulingPreference LoopAA::getSchedulingPreference() const
  {
    return Normal;
  }

  LoopAA::TemporalRelation LoopAA::Rev(TemporalRelation a)
  {
    switch(a)
    {
      case Before:
        return After;

      case After:
        return Before;

      case Same:
      default:
        return Same;
    }
  }

  LoopAA::AliasResult LoopAA::alias(const Value *ptrA, unsigned sizeA,
                                    TemporalRelation rel, const Value *ptrB,
                                    unsigned sizeB, const Loop *L, Remedies &R,
                                    DesiredAliasResult dAliasRes) {
    assert(nextAA && "Failure in chaining to next LoopAA; did you remember to add -no-loop-aa?");
    return nextAA->alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);
  }


  LoopAA::ModRefResult LoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L, Remedies &R)
  {
    assert(nextAA && "Failure in chaining to next LoopAA; did you remember to add -no-loop-aa?");
    return nextAA->modref(A,rel,ptrB,sizeB,L,R);
  }


  LoopAA::ModRefResult LoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L, Remedies &R)
  {
    assert(nextAA && "Failure in chaining to next LoopAA; did you remember to add -no-loop-aa?");
    return nextAA->modref(A,rel,B,L,R);
  }

  bool LoopAA::pointsToConstantMemory(const Value *P, const Loop *L) {
    assert(nextAA && "Failure in chaining to next LoopAA; did you remember to add -no-loop-aa?");
    return nextAA->pointsToConstantMemory(P, L);
  }

  bool LoopAA::canBasicBlockModify(const BasicBlock &BB, TemporalRelation Rel,
                                   const Value *Ptr, unsigned Size,
                                   const Loop *L, Remedies &R) {
    return canInstructionRangeModify(BB.front(), Rel, BB.back(), Ptr, Size, L,
                                     R);
  }

  bool LoopAA::canInstructionRangeModify(const Instruction &I1,
                                         TemporalRelation Rel,
                                         const Instruction &I2,
                                         const Value *Ptr, unsigned Size,
                                         const Loop *L, Remedies &R) {
    assert(I1.getParent() == I2.getParent() &&
           "Instructions not in same basic block!");
    BasicBlock::const_iterator I(I1);
    BasicBlock::const_iterator E(I2);
    ++E;  // Convert from inclusive to exclusive range.

    for (; I != E; ++I) // Check every instruction in range
    {
      const Instruction *Inst = &*I;
      if (modref(Inst, Rel, Ptr, Size, L, R) & Mod)
        return true;
    }
    return false;
  }

  bool LoopAA::mayModInterIteration(const Instruction *A, const Instruction *B,
                                    const Loop *L, Remedies &R) {

    if (A->mayWriteToMemory()) {
      ModRefResult a2b = modref(A, Before, B, L, R);
      if (a2b & LoopAA::Mod)
        return true;
    }

    if (B->mayWriteToMemory()) {
      ModRefResult b2a = modref(B, After, A, L, R);
      if(b2a & Mod)
        return true;
    }

    return false;
  }

  void LoopAA::dump() const
  {
    print(errs());
  }

  void LoopAA::print(raw_ostream &out) const
  {
    out << "LoopAA Stack, top to bottom:\n";

    for(const LoopAA *i=this; i!=0; i=i->nextAA)
    {
      out << "\to " << i->getLoopAAName() << '\n';
    }
  }

  static const Function *getParent(const Value *V) {
    if (const Instruction *inst = dyn_cast<Instruction>(V))
      return inst->getParent()->getParent();

    if (const Argument *arg = dyn_cast<Argument>(V))
      return arg->getParent();

    return NULL;
  }

  bool LoopAA::isInterprocedural(const Value *O1, const Value *O2) {

    const Function *F1 = getParent(O1);
    const Function *F2 = getParent(O2);

    return F1 && F2 && F1 != F2;
  }

  void LoopAA::stackHasChanged()
  {
    uponStackChange();

    if( nextAA )
      nextAA->stackHasChanged();
  }

  void LoopAA::uponStackChange() {}

//------------------------------------------------------------------------
// Methods of NoLoopAA

  NoLoopAA::NoLoopAA() : LoopAA(), ModulePass(ID) {}

  void NoLoopAA::getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< TargetLibraryInfoWrapperPass >();
    au.setPreservesAll();
  }

  LoopAA::SchedulingPreference NoLoopAA::getSchedulingPreference() const
  {
    return SchedulingPreference(Bottom-1);
  }

  bool NoLoopAA::runOnModule(Module &mod)
  {
    const DataLayout *t = &mod.getDataLayout();
    TargetLibraryInfoWrapperPass *tliWrap = &getAnalysis< TargetLibraryInfoWrapperPass>();
    TargetLibraryInfo *ti = &tliWrap->getTLI();

    InitializeLoopAA(t,ti,0);
    return false;
  }

  LoopAA::AliasResult NoLoopAA::alias(const Value *ptrA, unsigned sizeA,
                                      TemporalRelation rel, const Value *ptrB,
                                      unsigned sizeB, const Loop *L,
                                      Remedies &R,
                                      DesiredAliasResult dAliasRes) {
    LLVM_DEBUG(errs() << "NoLoopAA\n");
    return MayAlias;
  }


  LoopAA::ModRefResult NoLoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L, Remedies &R)
  {
    LLVM_DEBUG(errs() << "NoLoopAA\n");
    if( ! A->mayReadOrWriteMemory() )
      return NoModRef;
    else if( ! A->mayReadFromMemory() )
      return Mod;
    else if( ! A->mayWriteToMemory() )
      return Ref;
    else
      return ModRef;
  }


  LoopAA::ModRefResult NoLoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L, Remedies &R)
  {
    LLVM_DEBUG(errs() << "NoLoopAA\n");

    if( ! A->mayReadOrWriteMemory() || ! B->mayReadOrWriteMemory() )
      return NoModRef;
    else if( ! A->mayReadFromMemory() )
      return Mod;
    else if( ! A->mayWriteToMemory() )
      return Ref;
    else
      return ModRef;
  }

  bool NoLoopAA::pointsToConstantMemory(const Value *P, const Loop *L) {
    return false;
  }

//------------------------------------------------------------------------
// Methods of AAToLoopAA

  /// Conservatively raise an llvm::AliasResult
  /// to a liberty::LoopAA::AliasResult.
  AAToLoopAA::AliasResult AAToLoopAA::Raise(llvm::AliasResult ar)
  {
    switch(ar)
    {
      case llvm::NoAlias:
        return LoopAA::NoAlias;

      case llvm::MustAlias:
        return LoopAA::MustAlias;

      case llvm::MayAlias:
      default:
        return LoopAA::MayAlias;
    }
  }

  /// Conservatively raise an llvm::AliasAnalysis::ModRefResult
  /// to a liberty::LoopAA::ModRefResult.
  AAToLoopAA::ModRefResult AAToLoopAA::Raise(llvm::ModRefInfo mr)
  {
    switch(mr)
    {
      case llvm::MRI_NoModRef:
        return LoopAA::NoModRef;
      case llvm::MRI_Ref:
        return LoopAA::Ref;
      case llvm::MRI_Mod:
        return LoopAA::Mod;
      case llvm::MRI_ModRef:
      default:
        return LoopAA::ModRef;
    }
  }


  AAToLoopAA::AAToLoopAA() : FunctionPass(ID), LoopAA(), AA(0) {}

  /// Determine if L contains I, and no subloops of L contain I.
  static bool isInnermostContainingLoop(const Loop *L, const Instruction *I)
  {
    const BasicBlock *p = I->getParent();

    if( !L->contains(p) )
      return false;

    for(Loop::iterator i=L->begin(), e=L->end(); i!=e; ++i)
    {
      Loop *subloop = *i;
      if( subloop->contains(p) )
        return false;
    }

    return true;
  }

  /// Determine if llvm::AliasAnalysis is valid for this query.
  static bool isValid(const Loop *L, const Instruction *I)
  {
    return !L || isInnermostContainingLoop(L,I);
  }

  static bool isValid(const Loop *L, const Value *P) {

    if(const Instruction *I = dyn_cast<Instruction>(P))
      return isValid(L, I);

    return true;
  }

  void AAToLoopAA::getAnalysisUsage(AnalysisUsage &au) const
  {
    LoopAA::getAnalysisUsage(au);
    au.addRequired< AAResultsWrapperPass >();
    au.setPreservesAll();
  }

  bool AAToLoopAA::runOnFunction(Function &fcn)
  {
    const DataLayout &DL = fcn.getParent()->getDataLayout();
    InitializeLoopAA(this, DL);
    AAResultsWrapperPass &aliasWrap = getAnalysis<AAResultsWrapperPass>();
    AA = &aliasWrap.getAAResults();
    return false;
  }

  LoopAA::AliasResult AAToLoopAA::alias(const Value *ptrA, unsigned sizeA,
                                        TemporalRelation rel, const Value *ptrB,
                                        unsigned sizeB, const Loop *L,
                                        Remedies &R,
                                        DesiredAliasResult dAliasRes) {
    LLVM_DEBUG(errs() << "AAToLoopAA\n");
    if( rel == Same && isValid(L, ptrA) && isValid(L, ptrB) )
    {
      AliasResult r = Raise( AA->alias(ptrA,sizeA, ptrB,sizeB) );
      if( r != MayAlias )
        return r;
    }

    return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);
  }

  LoopAA::ModRefResult AAToLoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L, Remedies &R)
  {
    LLVM_DEBUG(errs() << "AAToLoopAA\n");
    // llvm::AliasAnalysis is only valid for innermost
    // loop!
    if( rel == Same && isValid(L,A) && isValid(L,ptrB) )
    {
      ModRefResult r = Raise( AA->getModRefInfo(A,ptrB,sizeB) );
      if( r == NoModRef )
        return r;
      else
        return ModRefResult( r & LoopAA::modref(A,rel,ptrB,sizeB,L,R) );
    }

    // Couldn't say anything specific; chain to lower analyses.
    return LoopAA::modref(A,rel,ptrB,sizeB,L,R);
  }

  LoopAA::ModRefResult AAToLoopAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L,
    Remedies &R)
  {
    // Why do we sometimes reverse queries:
    //
    // Two situations:
    //    load/store vs call
    //    intrinsic vs intrinsic
    //
    // llvm::AliasAnalysis is very asymmetric.
    // You always get better results with call vs load/store than load/store vs call.

    LLVM_DEBUG(errs() << "AAToLoopAA\n");
    // llvm::AliasAnalysis is only valid for innermost
    // loop!
    if( rel == Same )
    {
      if( isValid(L,A) || isValid(L,B) )
      {
        CallSite csA = getCallSite(const_cast<Instruction*>(A));
        CallSite csB = getCallSite(const_cast<Instruction*>(B));

        if( csA.getInstruction() && csB.getInstruction() )
        {
          ModRefResult r = Raise( AA->getModRefInfo(ImmutableCallSite(A),ImmutableCallSite(B)) );
          if( r == NoModRef )
            return NoModRef;

          else if( isa<IntrinsicInst>(A)
          &&       isa<IntrinsicInst>(B)
          &&       AA->getModRefInfo(ImmutableCallSite(B),ImmutableCallSite(A)) == llvm::MRI_NoModRef )
            // Conservatively reverse the query (see note at top of fcn)
            return NoModRef;

          else
            return ModRefResult( r & LoopAA::modref(A,rel,B,L,R) );
        }
        else if( csB.getInstruction() )
        {
          const Value *ptrA = getMemOper(A);
          PointerType *pty = cast<PointerType>( ptrA->getType() );
          const unsigned sizeA = getDataLayout()->getTypeSizeInBits( pty->getElementType() ) / 8;

          // Conservatively reverse the query (see note a t top of fcn)
          ModRefResult r = Raise( AA->getModRefInfo(B,ptrA,sizeA) );
          if( r == NoModRef )
            return r;

          else
            return LoopAA::modref(A,rel,B,L,R);

        }
        else if( const Value *ptrB = getMemOper(B) )
        {
          PointerType *pty = cast<PointerType>( ptrB->getType() );
          const unsigned sizeB = getDataLayout()->getTypeSizeInBits( pty->getElementType() ) / 8;

          ModRefResult r = Raise( AA->getModRefInfo(A, ptrB,sizeB) );
          if( r == NoModRef )
            return r;
          else
            return ModRefResult( r & LoopAA::modref(A,rel,B,L,R) );
        }
      }
    }

    // Couldn't say anything specific; chain to lower analyses.
    return LoopAA::modref(A,rel,B,L,R);
  }

//------------------------------------------------------------------------
// Methods of EvalLoopAA

#undef DEBUG_TYPE
#define DEBUG_TYPE "evalloopaa"

  EvalLoopAA::EvalLoopAA()
    : FunctionPass(ID)
  {
    totals[0][0] = totals[0][1] = totals[0][2] = totals[0][3] = 0;
    totals[1][0] = totals[1][1] = totals[1][2] = totals[1][3] = 0;
  }

  static void printStats(StringRef prefix, StringRef prefix2, unsigned *array)
  {
      const unsigned no=array[0], mod=array[1], ref=array[2], modref=array[3];

      float sum = (no + mod + ref + modref)/100.;

      char buffer[100];
      snprintf(buffer,100, "%s %s No Mod/Ref: %5d    %3.1f\n",
        prefix.data(), prefix2.data(), no, no/sum);
      errs() << buffer;
      snprintf(buffer,100, "%s %s    Mod    : %5d    %3.1f\n",
        prefix.data(), prefix2.data(), mod, mod/sum);
      errs() << buffer;
      snprintf(buffer,100, "%s %s        Ref: %5d    %3.1f\n",
        prefix.data(), prefix2.data(), ref, ref/sum);
      errs() << buffer;
      snprintf(buffer,100, "%s %s    Mod+Ref: %5d    %3.1f\n",
        prefix.data(), prefix2.data(), modref, modref/sum);
      errs() << buffer;
  }

  EvalLoopAA::~EvalLoopAA()
  {
    printStats("Module", "INTRA", totals[0]);
    printStats("Module", "INTER", totals[1]);
  }

  void EvalLoopAA::getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< LoopAA >();
    au.addRequired< LoopInfoWrapperPass >();
    au.setPreservesAll();
  }

  bool EvalLoopAA::runOnFunction(Function &fcn)
  {
    fcnTotals[0][0] = fcnTotals[0][1] = fcnTotals[0][2] = fcnTotals[0][3] = 0;
    fcnTotals[1][0] = fcnTotals[1][1] = fcnTotals[1][2] = fcnTotals[1][3] = 0;

    LoopInfo  &li = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

    std::vector<Loop*> loops( li.begin(), li.end() );
    while( ! loops.empty() ) {
      Loop *loop = loops.back();
      loops.pop_back();

      runOnLoop(loop);

      // append all sub-loops to the work queue
      loops.insert( loops.end(),
        loop->getSubLoops().begin(),
        loop->getSubLoops().end() );
    }

    LLVM_DEBUG(
      errs() << "Results of LoopAA on function: "
             << fcn.getName() << ":\n";
      printStats(fcn.getName().str().c_str(), "INTRA", fcnTotals[0]);
      printStats(fcn.getName().str().c_str(), "INTER", fcnTotals[1]);
    );
    return false;
  }

  bool EvalLoopAA::runOnLoop(Loop *L)
  {
    LoopAA *loopaa = getAnalysis< LoopAA >().getTopAA();

    loopTotals[0][0] = loopTotals[0][1] = loopTotals[0][2] = loopTotals[0][3] = 0;
    loopTotals[1][0] = loopTotals[1][1] = loopTotals[1][2] = loopTotals[1][3] = 0;

    Remedies R;

    // For every pair of instructions in this loop;
    for(Loop::block_iterator i=L->block_begin(), e=L->block_end(); i!=e; ++i)
    {
      const BasicBlock *bb = *i;
      for(BasicBlock::const_iterator j=bb->begin(), f=bb->end(); j!=f; ++j)
      {
        const Instruction *i1 = &*j;

        if( !i1->mayReadFromMemory() && !i1->mayWriteToMemory() )
          continue;

        for(Loop::block_iterator k=L->block_begin(); k!=e; ++k)
        {
          const BasicBlock *bb2 = *k;
          for(BasicBlock::const_iterator l=bb2->begin(), g=bb2->end(); l!=g; ++l)
          {
            const Instruction *i2 = &*l;

            if( !i2->mayReadFromMemory() && !i2->mayWriteToMemory() )
              continue;

            LLVM_DEBUG(errs() << "Query:\n\t" << *i1
                         <<       "\n\t" << *i2 << '\n');

            // don't ask reflexive, intra-iteration queries.
            if( i1 != i2 )
            {
              switch( loopaa->modref(i1,LoopAA::Same,i2,L,R) )
              {
                case LoopAA::NoModRef:
                  LLVM_DEBUG(errs() << "\tIntra: NoModRef\n");
                  ++loopTotals[0][0];
                  ++fcnTotals[0][0];
                  ++totals[0][0];
                  break;
                case LoopAA::Mod:
                  LLVM_DEBUG(errs() << "\tIntra: Mod\n");
                  ++loopTotals[0][1];
                  ++fcnTotals[0][1];
                  ++totals[0][1];
                  break;
                case LoopAA::Ref:
                  LLVM_DEBUG(errs() << "\tIntra: Ref\n");
                  ++loopTotals[0][2];
                  ++fcnTotals[0][2];
                  ++totals[0][2];
                  break;
                case LoopAA::ModRef:
                  LLVM_DEBUG(errs() << "\tIntra: ModRef\n");
                  ++loopTotals[0][3];
                  ++fcnTotals[0][3];
                  ++totals[0][3];
                  break;
              }
            }

            switch( loopaa->modref(i1,LoopAA::Before,i2,L,R) )
            {
              case LoopAA::NoModRef:
                LLVM_DEBUG(errs() << "\tInter: NoModRef\n");
                ++loopTotals[1][0];
                ++fcnTotals[1][0];
                ++totals[1][0];
                break;
              case LoopAA::Mod:
                LLVM_DEBUG(errs() << "\tInter: Mod\n");
                ++loopTotals[1][1];
                ++fcnTotals[1][1];
                ++totals[1][1];
                break;
              case LoopAA::Ref:
                LLVM_DEBUG(errs() << "\tInter: Ref\n");
                ++loopTotals[1][2];
                ++fcnTotals[1][2];
                ++totals[1][2];
                break;
              case LoopAA::ModRef:
                LLVM_DEBUG(errs() << "\tInter: ModRef\n");
                ++loopTotals[1][3];
                ++fcnTotals[1][3];
                ++totals[1][3];
                break;
            }
          }
        }

      }
    }

    LLVM_DEBUG(
      BasicBlock *header = L->getHeader();
      StringRef loopName = header->getName().str().c_str();
      errs() << "Results of LoopAA on loop: "
             << header->getParent()->getName()
             << "::" << header->getName() << ":\n";

      printStats(loopName, "INTRA", loopTotals[0]);
      printStats(loopName, "INTER", loopTotals[1]);
    );
    return false;
  }
}

