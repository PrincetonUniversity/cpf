#define DEBUG_TYPE "callsite-breadth-combinator-aa"

#include "liberty/Analysis/Introspection.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/FindSource.h"
#include "liberty/Analysis/KillFlow.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/QueryCacheing.h"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/CaptureUtil.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"


using namespace llvm;
using namespace liberty;

STATISTIC(numHitsIF,    "Num cache hits (I-F)");
STATISTIC(numHitsFI,    "Num cache hits (F-I)");
STATISTIC(numHitsFP,    "Num cache hits (F-P)");
STATISTIC(numRecurs,    "Num expanded callsites");
STATISTIC(numOps,       "Num memory ops within an expanded callsite");
STATISTIC(numKilledOps, "Num killed memory ops within an expanded callsite");

typedef DenseMap< FcnPtrKey, LoopAA::ModRefResult > FcnPtrCache;
typedef DenseMap< InstFcnKey, LoopAA::ModRefResult > InstFcnCache;
typedef DenseMap< FcnInstKey, LoopAA::ModRefResult > FcnInstCache;




typedef Value::const_user_iterator UseIt;
typedef DenseSet<const Value *> ValueSet;

class CallsiteBreadthCombinator : public ModulePass, public liberty::LoopAA
{
  InstFcnCache instFcnCache;
  FcnInstCache fcnInstCache;
  FcnPtrCache  fcnPtrCache;

  const DataLayout *DL;

  unsigned countPtrArgs(const Instruction *inst)
  {
    CallSite cs = getCallSite(inst);

    unsigned n=0;
    for(User::const_op_iterator i=cs.arg_begin(), e=cs.arg_end(); i!=e; ++i)
    {
      const Value *op = *i;
      Type *opTy = op->getType();
      if( opTy->isIntegerTy() || opTy->isFloatingPointTy() )
        continue;
      ++n;
    }

    return n;
  }

  ModRefResult recur(const Instruction *op, TemporalRelation Rel,
                     const Value *p2, unsigned s2, const Loop *L, Remedies &R) {
    return getTopAA()->modref(op, Rel, p2, s2, L, R);
  }

  ModRefResult recur(const Instruction *i1, TemporalRelation Rel,
                     const Instruction *i2, const Loop *L, Remedies &R) {
    return getTopAA()->modref(i1, Rel, i2, L, R);
  }

  ModRefResult recurLeft(const Function *fcn, TemporalRelation Rel,
                         const Instruction *i2, const Loop *L, Remedies &R) {
    FcnInstKey key(fcn,Rel,i2,L);
    if( fcnInstCache.count(key) )
    {
      ++numHitsFI;
      return fcnInstCache[key];
    }

    // Avoid infinite recursion.  We will put in a more precise answer later.
    fcnInstCache[key] = ModRef;

    ++numRecurs;
    ModRefResult result = NoModRef;

    // Update the query for non-callsite instructions first.
    // We will visit nested callsites later.
    for(const_inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      // Possibly break early if it can't get worse.
      if( result == ModRef )
        break;

      const Instruction *instFromCallee = &*i;
      if( !instFromCallee->mayReadFromMemory() && !instFromCallee->mayWriteToMemory() )
        continue;
      if( isa< CallInst >(instFromCallee) || isa< InvokeInst >(instFromCallee) )
        continue;

      // Inst is a memory operation.
      ++numOps;

      if( const StoreInst *store = dyn_cast< StoreInst >(instFromCallee) )
      {
        if( isa< AllocaInst >( GetUnderlyingObject(store->getPointerOperand(), *DL, 0) ) )
        {
          // Allocas are patently local
          ++numKilledOps;
          continue;
        }
      }

      else if( const LoadInst *load = dyn_cast< LoadInst >(instFromCallee) )
      {
        if( isa< AllocaInst >( GetUnderlyingObject( load->getPointerOperand(), *DL, 0 ) ) )
        {
          // Allocas are patently local
          ++numKilledOps;
          continue;
        }
      }

      ModRefResult old = result;
      result = ModRefResult(result | recur(instFromCallee,Rel,i2,L,R) );

      INTROSPECT(
        if( result != old )
        {
          errs() << "CallsiteBreadthCombinator: recurLeft(" << fcn->getName() << ", " << *i2 << ") vs op:\n";
          errs() << "\tChanged " << old << " => " << result << '\n';
          errs() << "\t\t" << *instFromCallee << '\n';
        }
      );
    }

    // Now the nested callsites.
    for(const_inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      // possibly break early.
      if( result == ModRef )
        break;

      const Instruction *instFromCallee = &*i;
      if( !instFromCallee->mayReadFromMemory() && !instFromCallee->mayWriteToMemory() )
        continue;
      CallSite nested = getCallSite(instFromCallee);
      if( !nested.getInstruction() )
        continue;

      ++numOps;

      ModRefResult old = result;
      result = ModRefResult(result | recur(instFromCallee,Rel,i2,L,R) );

      INTROSPECT(
        if( result != old )
        {
          errs() << "CallsiteBreadthCombinator: recurLeft(" << fcn->getName() << ", " << *i2 << ") vs op:\n";
          errs() << "\tChanged " << old << " => " << result << '\n';
          errs() << "\t\t" << *instFromCallee << '\n';
        }
      );
    }

    return fcnInstCache[key] = result;
  }

  ModRefResult recurLeft(const Function *fcn, TemporalRelation Rel,
                         const Value *p2, unsigned s2, const Loop *L,
                         Remedies &R) {
    FcnPtrKey key(fcn, Rel, p2,s2, L);
    if( fcnPtrCache.count(key) )
    {
      ++numHitsFP;
      return fcnPtrCache[key];
    }

    // Avoid infinite recursion.  We'll put in a more precise
    // result later.
    fcnPtrCache[key] = ModRef;

    ++numRecurs;
    ModRefResult result = NoModRef;

    // Update the query for non-callsite instructions first.
    // We will visit nested callsites later.
    for(const_inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      // Possibly break early if it can't get worse.
      if( result == ModRef )
        break;

      const Instruction *instFromCallee = &*i;
      if( !instFromCallee->mayReadFromMemory() && !instFromCallee->mayWriteToMemory() )
        continue;
      if( isa< CallInst >(instFromCallee) || isa< InvokeInst >(instFromCallee) )
        continue;

      // Inst is a memory operation.
      ++numOps;

      if( const StoreInst *store = dyn_cast< StoreInst >(instFromCallee) )
      {
        if( isa< AllocaInst >( GetUnderlyingObject(store->getPointerOperand(), *DL, 0) ) )
        {
          // Allocas are patently local
          ++numKilledOps;
          continue;
        }
      }

      else if( const LoadInst *load = dyn_cast< LoadInst >(instFromCallee) )
      {
        if( isa< AllocaInst >( GetUnderlyingObject( load->getPointerOperand(), *DL, 0 ) ) )
        {
          // Allocas are patently local
          ++numKilledOps;
          continue;
        }
      }

      ModRefResult old = result;
      result = ModRefResult(result | recur(instFromCallee,Rel,p2,s2,L,R) );

      INTROSPECT(
        if( result != old )
        {
          errs() << "CallsiteBreadthCombinator: recurLeft(" << fcn->getName() << ", " << *p2 << ") vs ptr:\n";
          errs() << "\tChanged " << old << " => " << result << '\n';
          errs() << "\t\t" << *instFromCallee << '\n';
        }
      );
    }

    // Now the nested callsites.
    for(const_inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      // possibly break early.
      if( result == ModRef )
        break;

      const Instruction *instFromCallee = &*i;
      if( !instFromCallee->mayReadFromMemory() && !instFromCallee->mayWriteToMemory() )
        continue;
      CallSite nested = getCallSite(instFromCallee);
      if( !nested.getInstruction() )
        continue;

      ++numOps;

      ModRefResult old = result;
      result = ModRefResult(result | recur(instFromCallee,Rel,p2,s2,L,R) );

      INTROSPECT(
        if( result != old )
        {
          errs() << "CallsiteBreadthCombinator: recurLeft(" << fcn->getName() << ", " << *p2 << ") vs ptr:\n";
          errs() << "\tChanged " << old << " => " << result << '\n';
          errs() << "\t\t" << *instFromCallee << '\n';
        }
      );
    }

    return fcnPtrCache[key] = result;
  }

  ModRefResult recurRight(const Instruction *i1, TemporalRelation Rel,
                          const Function *fcn, const Loop *L, Remedies &R) {
    InstFcnKey key(i1,Rel,fcn,L);
    if( instFcnCache.count(key) )
    {
      ++numHitsIF;
      return instFcnCache[key];
    }

    // Avoid infinite recursion.  We will put in a more precise answer later.
    instFcnCache[key] = ModRef;

    KillFlow &killFlow = getAnalysis< KillFlow >();

    ++numRecurs;
    ModRefResult result = NoModRef;

    // Update the query for non-callsite instructions first.
    // We will visit nested callsites later.
    for(const_inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      // Possibly break early if it can't get worse.
      if( result == ModRef )
        break;

      const Instruction *instFromCallee = &*i;
      if( !instFromCallee->mayReadFromMemory() && !instFromCallee->mayWriteToMemory() )
        continue;
      if( isa< CallInst >(instFromCallee) || isa< InvokeInst >(instFromCallee) )
        continue;

      ++numOps;

      if( const StoreInst *store = dyn_cast< StoreInst >(instFromCallee) )
      {
        if( isa< AllocaInst >( GetUnderlyingObject( store->getPointerOperand(), *DL, 0) ) )
        {
          // Allocas are patently local
          ++numKilledOps;
          continue;
        }
      }

      else if( const LoadInst *load = dyn_cast< LoadInst >(instFromCallee) )
      {
        if( isa< AllocaInst >( GetUnderlyingObject( load->getPointerOperand(), *DL, 0 ) ) )
        {
          // Allocas are patently local
          ++numKilledOps;
          continue;
        }

        if( killFlow.pointerKilledBefore(0, load->getPointerOperand(), load) )
        {
          ++numKilledOps;
          continue;
        }
      }

      // Inst is a memory operation.
      ModRefResult old = result;
      result = ModRefResult(result | recur(i1,Rel,instFromCallee,L,R) );

      INTROSPECT(
        if( result != old )
        {
          errs() << "CallsiteBreadthCombinator: recurRight(" << *i1 << ", " << fcn->getName() << "):\n";
          errs() << "\tChanged " << old << " => " << result << '\n';
          errs() << "\t\t" << *instFromCallee << '\n';
        }
      );
    }

    // Now the nested callsites.
    for(const_inst_iterator i=inst_begin(fcn), e=inst_end(fcn); i!=e; ++i)
    {
      // possibly break early.
      if( result == ModRef )
        break;

      const Instruction *instFromCallee = &*i;
      if( !instFromCallee->mayReadFromMemory() && !instFromCallee->mayWriteToMemory() )
        continue;
      CallSite nested = getCallSite(instFromCallee);
      if( !nested.getInstruction() )
        continue;

      // top-query
      ++numOps;
      ModRefResult old = result;
      result = ModRefResult(result | recur(i1,Rel,instFromCallee,L,R) );

      INTROSPECT(
        if( result != old )
        {
          errs() << "CallsiteBreadthCombinator: recurRight(" << *i1 << ", " << fcn->getName() << "):\n";
          errs() << "\tChanged " << old << " => " << result << '\n';
          errs() << "\t\t" << *instFromCallee << '\n';
        }
      );
    }

    return instFcnCache[key] = result;
  }


  const Function *getEligibleFunction(const Instruction *i) const
  {
    CallSite cs = getCallSite(i);
    if( !cs.getInstruction() )
      return 0;

    const Function *f = cs.getCalledFunction();
    if( !f )
      return 0;

    if( f->isDeclaration() )
      return 0;

    return f;
  }

protected:
  virtual void uponStackChange()
  {
    instFcnCache.clear();
    fcnInstCache.clear();
    fcnPtrCache.clear();
  }

public:
  static char ID;
  CallsiteBreadthCombinator() : ModulePass(ID), instFcnCache(), fcnInstCache(), fcnPtrCache(), DL() {}

  virtual bool runOnModule(Module &M)
  {
    LLVM_DEBUG(errs() << "callsite breadth comb callsites " << WatchCallsitePair << " "
      << FirstCallee << " "
      << SecondCallee << "\n");

    DL =  &M.getDataLayout();
    InitializeLoopAA(this, *DL);
    return false;
  }

  virtual SchedulingPreference getSchedulingPreference() const
  {
    return SchedulingPreference( Normal - 1 );
  }

  StringRef getLoopAAName() const
  {
    return "callsite-breadth-combinator-aa";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const
  {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired< KillFlow >();
    AU.setPreservesAll();        // Does not transform code
  }

  ModRefResult modref(const Instruction *i1, TemporalRelation Rel,
                      const Instruction *i2, const Loop *L, Remedies &R) {
    const Function *f1 = getEligibleFunction(i1),
                   *f2 = getEligibleFunction(i2);

    // Maybe turn-on introspection
    bool introspect = false;

    if( WatchCallsitePair )
    {
        if( f1 && FirstCallee == f1->getName()
        &&  f2 && SecondCallee == f2->getName() )
          introspect = true;
    }
    else if( WatchCallsite2Store )
    {
      const StoreInst *st2 = dyn_cast< StoreInst >(i2);

      if( f1 && f1->getName() == FirstCallee )
        if( st2 && st2->getPointerOperand()->getName() == StorePtrName )
          introspect = true;
    }

    else if( WatchStore2Callsite )
    {
      const StoreInst *st1 = dyn_cast< StoreInst >(i1);

      if( f2 && f2->getName() == SecondCallee )
        if( st1 && st1->getPointerOperand()->getName() == StorePtrName )
          introspect = true;
    }

    if( introspect )
      enterIntrospectionRegion();

    INTROSPECT(ENTER(i1,Rel,i2,L));

    ModRefResult result = LoopAA::modref(i1,Rel,i2,L,R);
    if( result == NoModRef )
    {
      INTROSPECT(EXIT(i1,Rel,i2,L,result));
      return result;
    }

    if( f1 && f2 )
    {
      // We would like to inline one of the two callsites.


      // If we can inline both, we must choose.
      // Since our major source of imprecision is function
      // arguments, we choose to inline the one with
      // fewer pointer operands.
      if( countPtrArgs(i1) < countPtrArgs(i2) )
        // We prefer to inline CS1
        result = ModRefResult(result & recurLeft(f1,Rel,i2,L,R) );

      else
        // We prefer to inline CS2
        result = ModRefResult(result & recurRight(i1,Rel,f2,L,R) );
    }

    else if( f1 )
    {
      // We can only inline CS1.
      result = ModRefResult(result & recurLeft(f1,Rel,i2,L,R) );
    }

    else if( f2 )
    {
      // We can only inline CS2.
      result = ModRefResult(result & recurRight(i1,Rel,f2,L,R) );
    }

    INTROSPECT(EXIT(i1,Rel,i2,L,result));

    if( introspect )
      exitIntrospectionRegion();
    return result;
  }

  ModRefResult modref(const Instruction *i1, TemporalRelation Rel,
                      const Value *p2, unsigned s2, const Loop *L,
                      Remedies &R) {
    INTROSPECT( ENTER(i1,Rel,p2,s2,L) );
    ModRefResult result = LoopAA::modref(i1,Rel,p2,s2,L, R);

    if( result != NoModRef )
      if( const Function *fcn = getEligibleFunction(i1) )
        result = ModRefResult(result & recurLeft(fcn,Rel,p2,s2,L,R) );

    INTROSPECT( EXIT(i1,Rel,p2,s2,L,result) );
    return result;
  }


  /// getAdjustedAnalysisPointer - This method is used when a pass implements
  /// an analysis interface through multiple inheritance.  If needed, it
  /// should override this to adjust the this pointer as needed for the
  /// specified pass info.
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI)
  {
    if (PI == &LoopAA::ID)
      return (LoopAA*)this;
    return this;
  }
};

char CallsiteBreadthCombinator::ID = 0;

static RegisterPass<CallsiteBreadthCombinator>
XX("callsite-breadth-combinator-aa", "Alias analysis which substitutes body of callees for callsites", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(XX);

