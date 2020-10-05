#define DEBUG_TYPE "no-capture-global-aa"

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/Debug.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include "liberty/Analysis/FindSource.h"
#include "liberty/Analysis/LoopAA.h"
#include "scaf/Utilities/CaptureUtil.h"
#include "scaf/Utilities/FindUnderlyingObjects.h"

#include "liberty/Analysis/Introspection.h"

using namespace liberty;
using namespace llvm;

STATISTIC(numQueries, "Queries");
STATISTIC(numNoAlias, "No-alias");

static bool isNonCapturedGlobal(const GlobalValue *G) {

  if(isa<GlobalAlias>(G))
    return false;

  if(!G->hasLocalLinkage())  {

    if(!liberty::FULL_UNIVERSAL)
      return false;

    if(G->isDeclaration())
      return false;
  }

  return !liberty::findAllCaptures(G);
}

static bool mayBeCaptured(const GlobalValue *gv )
{
  return  ! isNonCapturedGlobal(gv);
}

template<typename Iterator>
static bool findLoadedNoCaptureArgument(const Iterator &begin,
                                        const Iterator &end, const DataLayout &td) {

  for(Iterator value = begin; value != end; ++value) {
    if(liberty::findLoadedNoCaptureArgument(*value, td))
      return true;
  }

  return false;
}

class NoCaptureGlobalAA : public ModulePass, public liberty::ClassicLoopAA {
  const DataLayout *DL;
public:
  static char ID;
  NoCaptureGlobalAA() : ModulePass(ID) {}

  bool runOnModule(Module &M) {
    DL = &M.getDataLayout();
    InitializeLoopAA(this, *DL);
    return false;
  }

  // Determine if 'P1' must refer to a non-capture global,
  // and 'P2' must NOT refer to a non-capture global.
  bool cannotAlias(const Value *P1, const Value *P2) const
  {
    LLVM_DEBUG(errs() << "NoCaptureGlobalAA::cannotAlias(\n"
                 << "  P1=" << *P1 << ",\n"
                 << "  P2=" << *P2 << ") ?\n");
    // Does 'P1' refer to a non-capture global variable?
    UO uo1;
    GetUnderlyingObjects(P1,uo1,*DL);
    for(UO::iterator i=uo1.begin(), e=uo1.end(); i!=e; ++i)
    {
      const Value *object = *i;
      const GlobalValue *gv = dyn_cast< GlobalValue >(object);
      if( ! gv )
      {
        LLVM_DEBUG(errs() << "=> NO: P1 may refer to " << *object << ", which is not a global variable.\n");
        return false;
      }

      if( mayBeCaptured(gv) )
      {
        LLVM_DEBUG(errs() << "=> NO: P1 might refer to the may-capture global variable " << *gv << '\n');
        return false;
      }
    }

    // 'P1' definitely refers to a non-captured global.  Non-captured
    // globals are disjoint from any pointer loaded from memory.

    // Can we say that 'P2' does not refer to a no-capture global?
    UO uo2;
    GetUnderlyingObjects(P2,uo2,*DL);
    for(UO::iterator i=uo2.begin(), e=uo2.end(); i!=e; ++i)
    {
      const Value *object = *i;

      if( isa<ConstantPointerNull>(object) )
      {
        // Good: 'object' is a null.  P1 points to a global
        // variable, which cannot be null.
        LLVM_DEBUG(errs() << "   (Ok: P2 may be null)\n");
        continue;
      }

      else if( isa<LoadInst>(object) )
      {
        // Good: 'object' is a pointer loaded from memory.
        // The address of no-capture globals is never saved to
        // memory, so the loaded pointer must be disjoint from
        // all non-capture globals.
        LLVM_DEBUG(errs() << "   (Ok: P2 may refer to a load)\n");
        continue;
      }

      else if( const Argument *arg = dyn_cast< Argument >(object) )
      {
        // Hmm: this object is an argument.  In the worst case,
        // it could be anything...

        if( arg->hasNoCaptureAttr() )
        {
          // A pointer to a non-capture global is allowed
          // to flow into a non-capture argument of a callsite;
          // So, this argument may alias with a global variable
          // in 'uo1'.
          LLVM_DEBUG(errs() << "=> NO: P2 may refer to no-capture argument " << *arg << '\n');
          return false;
        }

        else
        {
          // This is a may-capture argument.
          // If a pointer flows to this argument, then that
          // pointer is NOT a non-capture pointer.
          // Thus, 'arg' and 'P1' must be disjoint.
          LLVM_DEBUG(errs() << "   (Ok: P2 may refer to a may-capture argument " << *arg << ")\n");
          continue;
        }
      }

      else if( const GlobalValue *gv = dyn_cast< GlobalValue >(object) )
      {
        if( isa<GlobalAlias>(gv) )
        {
          // 'P1' may also refer to 'gv'
          LLVM_DEBUG(errs() << "=> NO: Both P1,P2 may refer to " << *gv << '\n');
          return false;
        }

        // 'P2' may refer to the global variable 'gv'
        else if( isNonCapturedGlobal(gv) )
        {
          if( uo1.count(gv) )
          {
            // 'P1' may also refer to 'gv'
            LLVM_DEBUG(errs() << "=> NO: Both P1,P2 may refer to " << *gv << '\n');
            return false;
          }

          else
          {
            // 'P1' cannot refer to 'gv'
            LLVM_DEBUG(errs() << "   (Ok: P2 and not P1 may refer to no-capture global " << *gv << ")\n");
            continue;
          }
        }
        else
        {
          // This is NOT a no capture global,
          // thus, it is disjoint from all no-capture globals in 'uo1'.
          LLVM_DEBUG(errs() << "   (Ok: P2 may refer to may-capture global " << *gv << ")\n");
          continue;
        }
      }

      else
      {
        // Everything else: we cannot say for sure that this
        // object
        LLVM_DEBUG(errs() << "=> NO: P2 may refer to " << *object << '\n');
        return false;
      }
    }

    // 'P1' definitely refers to a non-captured global, AND
    // 'P2' definitely does NOT refer to a non-captured global.
    LLVM_DEBUG(errs() << "=> YES: disjoint.\n");
    return true;
  }

  virtual AliasResult
  aliasCheck(const Pointer &P1, TemporalRelation Rel, const Pointer &P2,
             const Loop *L, Remedies &R,
             DesiredAliasResult dAliasRes = DNoOrMustAlias) {

    if (dAliasRes == DMustAlias)
      return MayAlias;

    INTROSPECT(ENTER(P1,Rel,P2,L));
    ++numQueries;

    const Value *V1 = P1.ptr, *V2 = P2.ptr;

#if 1
    /* At a high level, we use this reasoning:
     * If the first object is a no-capture-global,
     * and the second object is a pointer loaded from memory,
     *  => they cannot alias.
     */

    if( cannotAlias(V1,V2) || cannotAlias(V2,V1) )
    {
      INTROSPECT(EXIT(P1,Rel,P2,L,NoAlias));
      ++numNoAlias;
      return  NoAlias;
    }

    INTROSPECT(EXIT(P1,Rel,P2,L,MayAlias));
    return MayAlias;
#endif

    // Thom's version
    {
      const Value *O1 = GetUnderlyingObject(V1, *DL);
      const Value *O2 = GetUnderlyingObject(V2, *DL);

      const GlobalValue *G1 = dyn_cast<GlobalValue>(O1);
      const GlobalValue *G2 = dyn_cast<GlobalValue>(O2);

      if(G1 && isNonCapturedGlobal(G1)) {

        if(isa<LoadInst>(O2) && !liberty::findLoadedNoCaptureArgument(V2, *DL))
        {
          INTROSPECT(EXIT(P1,Rel,P2,L,NoAlias));
          ++numNoAlias;
          return NoAlias;
        }

        if(isa<PHINode>(O2)) {
          liberty::ObjectSet Objects;
          liberty::findUnderlyingObjects(O2, Objects);
          if(!findLoadedNoCaptureArgument(Objects.begin(), Objects.end(), *DL))
          {
            INTROSPECT(EXIT(P1,Rel,P2,L,NoAlias));
            ++numNoAlias;
            return NoAlias;
          }
        }
      }

      if(G2 && isNonCapturedGlobal(G2)) {

        if(isa<LoadInst>(O1) && !liberty::findLoadedNoCaptureArgument(V1, *DL))
        {
          INTROSPECT(EXIT(P1,Rel,P2,L,NoAlias));
          ++numNoAlias;
          return NoAlias;
        }

        if(isa<PHINode>(O1)) {
          liberty::ObjectSet Objects;
          liberty::findUnderlyingObjects(O1, Objects);
          if(!findLoadedNoCaptureArgument(Objects.begin(), Objects.end(), *DL))
          {
            INTROSPECT(EXIT(P1,Rel,P2,L,NoAlias));
            ++numNoAlias;
            return NoAlias;
          }
        }
      }

      INTROSPECT(EXIT(P1,Rel,P2,L,MayAlias));
      return MayAlias;
    }
  }

  StringRef getLoopAAName() const {
    return DEBUG_TYPE;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
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

static RegisterPass<NoCaptureGlobalAA>
X("no-capture-global-aa", "Reason about non-captured globals", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char NoCaptureGlobalAA::ID = 0;


