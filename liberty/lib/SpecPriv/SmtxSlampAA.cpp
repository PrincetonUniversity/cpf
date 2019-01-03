#define DEBUG_TYPE "smtx-slamp-aa"

#include "SmtxSlampAA.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"

namespace liberty
{
namespace SpecPriv
{

  using namespace llvm;

  STATISTIC(numQueries,       "Num queries");
  STATISTIC(numEligible,      "Num eligible queries");
  STATISTIC(numNoForwardFlow, "Num forward no-flow results");
  STATISTIC(numNoReverseFlow, "Num reverse no-flow results");

  static cl::opt<unsigned> Threshhold(
    "smtx-slamp-threshhold", cl::init(0),
    cl::NotHidden,
    cl::desc("Maximum number of observed flows to report NoModRef"));

  LoopAA::AliasResult SmtxSlampAA::alias(
    const Value *ptrA, unsigned sizeA,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
  {
    return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L);
  }

  LoopAA::ModRefResult SmtxSlampAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L)
  {
    return LoopAA::modref(A,rel,ptrB,sizeB,L);
  }

  static bool isMemIntrinsic(const Instruction *inst)
  {
    return isa< MemIntrinsic >(inst);
  }

  static bool intrinsicMayRead(const Instruction *inst)
  {
    ImmutableCallSite cs(inst);
    StringRef  name = cs.getCalledFunction()->getName();
    if( name == "llvm.memset.p0i8.i32"
    ||  name == "llvm.memset.p0i8.i64" )
      return false;

    return true;
  }

  void SmtxSlampAA::queryAcrossCallsites(
    const Instruction* A, 
    TemporalRelation rel,
    const Instruction* B, 
    const Loop *L)
  {
    std::vector<const Instruction*> writes;
    std::vector<const Instruction*> reads;

    if ( isa<StoreInst>(A) || isMemIntrinsic(A) )
    {
      writes.push_back(A);
    }
    else 
    {
      const CallInst* ci = cast<CallInst>(A);
      smtxMan->collectWrites(ci->getCalledFunction(), writes);
    }

    if ( isa<LoadInst>(B) || isMemIntrinsic(B) )
    {
      reads.push_back(B);
    }
    else 
    {
      const CallInst* ci = cast<CallInst>(B);
      smtxMan->collectReads(ci->getCalledFunction(), reads);
    }

    for (unsigned i = 0 ; i < writes.size() ; i++)
    {
      for (unsigned j = 0 ; j < reads.size() ; j++)
      {
        IIKey key(A,rel,B,L);
        if (queried[key]) continue;
        queried[key] = true;

        getTopAA()->modref(A,rel,B,L);  
      }
    }
  }

  LoopAA::ModRefResult SmtxSlampAA::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L)
  {
    ++numQueries;

    SLAMPLoadProfile& slamp = smtxMan->getSlampResult();

    // Slamp profile data is loop sensitive.
    if( !L || !slamp.isTargetLoop(L) )
    {
      // Inapplicable
      //std::string space(getDepth()+1, ' ');
      //errs() << space << "si\n";
      return LoopAA::modref(A,rel,B,L);
    }

    // both instructions should be included in the target loop
    bool includeA = false;
    bool includeB = false;

    for (Loop::block_iterator bi = L->block_begin() ; bi != L->block_end() ; bi++)
    {
      if ( *bi == A->getParent() )
        includeA = true;
      if ( *bi == B->getParent() )
        includeB = true;
    }

    if ( !includeA || !includeB )
    {
      // Inapplicable
      //std::string space(getDepth()+1, ' ');
      //errs() << space << "si\n";
      return LoopAA::modref(A,rel,B,L);
    }

    ModRefResult result = ModRef;

    // Loop carried forward queries, or
    // Same queries.
    if( rel == Before || rel == Same )
    {
      // Slamp profile data is colected for loads, stores, and callistes.
      // Slamp only collect FLOW info. 
      // Thus, for Before/Same queries, we are looking
      // for Store/CallSite -> Load/CallSite

      if( isa<StoreInst>(A) )
        // Stores don't ref
        result = Mod;

      else if( isMemIntrinsic(A) )
      {
        if( intrinsicMayRead(A) )
          result = ModRef;
        else
          result = Mod;
      }

      else if( isa<CallInst>(A) )
      {
        result = ModRef;
      }
      else
      {
        // inapplicable
        result = LoopAA::modref(A,rel,B,L);
        return result;
      }

      if( isa<LoadInst>(B) )
      {
        // okay
      }
      else if( isMemIntrinsic(B) && intrinsicMayRead(B) )
      {
        // okay
      }
      else if ( isa<CallInst>(B) )
      {
        // okay
      }
      else
      {
        // inapplicable, as Slamp does not collect output dependence
        result = ModRefResult(result & LoopAA::modref(A,rel,B,L) );
        return result;
      }

      if( rel == Before )
      {
        ++numEligible;

        // Query profile data for a loop-carried flow from A to B
        if( slamp.numObsInterIterDep(L->getHeader(), B, A) <= Threshhold )
        {
          // No flow.
          result = ModRefResult(result & ~Mod);
          ++numNoForwardFlow;

          // Keep track of this
        
          //queryAcrossCallsites(A,Before,B,L);
          smtxMan->setAssumedLC(L,A,B);
        }
        else if ( slamp.isPredictableInterIterDep(L->getHeader(), B, A) )
        {
          // No flow.
          result = ModRefResult(result & ~Mod);
          ++numNoForwardFlow;

          slamp::PredMap predictions = slamp.getPredictions(L->getHeader(), B, A, true);
          for (slamp::PredMap::iterator i=predictions.begin(), e=predictions.end() ; i!=e ; ++i) 
          {
            LoadInst* li = i->first;

            if (i->second.type == slamp::LI_PRED)
            {
              smtxMan->setAssumedLC(L, A, li, B);
            }
            else if (i->second.type == slamp::LINEAR_PRED)
            {
              smtxMan->setAssumedLC(L, A, li, B, i->second.a, i->second.b, false);
            }
            else if (i->second.type == slamp::LINEAR_PRED_DOUBLE)
            {
              smtxMan->setAssumedLC(L, A, li, B, i->second.a, i->second.b, true);
            }
            else
            {
              assert(false);
            }
          }
        }
        else
        {
          //errs() << "--- SLAMP failed to speculate\n";
          //errs() << "    src : " ; A->dump();
          //errs() << "    dst : " ; B->dump();

          //slamp.dumpValuePredictionForEdge(L->getHeader(), B, A, true);
        }
      }

      else if( rel == Same )
      {
#if 0 // disable intra-iteration speculation

        ++numEligible;

        // Query profile data for an intra-iteration flow from A to B
        if( slamp.numObsIntraIterDep(L->getHeader(), B, A) <= Threshhold )
        {
          // No flow
          result = ModRefResult(result & ~Mod);
          ++numNoForwardFlow;

          // Keep track of this

          //queryAcrossCallsites(A,Same,B,L);
          smtxMan->setAssumedII(L,A,B);
        }
        else if (slamp.isPredictableIntraIterDep(L->getHeader(), B, A) )
        {
          // No flow
          result = ModRefResult(result & ~Mod);
          ++numNoForwardFlow;

          slamp::PredMap predictions = slamp.getPredictions(L->getHeader(), B, A, false);
          for (slamp::PredMap::iterator i=predictions.begin(), e=predictions.end() ; i!=e ; ++i) 
          {
            LoadInst* li = i->first;

            if (i->second.type == slamp::LI_PRED)
            {
              smtxMan->setAssumedII(L, A, li, B);
            }
            else if (i->second.type == slamp::LINEAR_PRED)
            {
              smtxMan->setAssumedII(L, A, li, B, i->second.a, i->second.b, false);
            }
            else if (i->second.type == slamp::LINEAR_PRED_DOUBLE)
            {
              smtxMan->setAssumedII(L, A, li, B, i->second.a, i->second.b, true);
            }
            else
            {
              assert(false);
            }
          }
        }
#endif
      }
    }

    // Loop carried reverse queries.
    else if( rel == After )
    {
      // Slamp profile data is colected for loads, stores, and callistes.
      // Slamp only collect FLOW info. 
      // Thus, for After queries, we are looking
      // for Store/CallSite -> Load/CallSite

      if( isa<LoadInst>(A) )
        // Anti or False: inapplicable
        result = Ref;

      else if( isMemIntrinsic(A) && intrinsicMayRead(A) )
        result = ModRef;

      else if( isa<CallInst>(A) )
        result = ModRef;

      else
      {
        // inapplicable
        result = LoopAA::modref(A,rel,B,L);
        return result;
      }

      // Again, only (Load/Callsite) vs (Store/CallSite)
      if( isa<StoreInst>(B) )
      {
        // good
      }
      else if( isMemIntrinsic(B) )
      {
        // good
      }
      else if ( isa<CallInst>(B) )
      {
        // good
      }
      else
      {
        // inapplicable
        result = ModRefResult(result & LoopAA::modref(A,rel,B,L));
        return result;
      }

      ++numEligible;
      // Query profile data for a loop-carried flow from B to A
      if( slamp.numObsInterIterDep(L->getHeader(), A, B) <= Threshhold )
      {
        // No flow.
        if( isa<LoadInst>(B) )
          result = ModRefResult(result & ~Ref);

        else if( isa<StoreInst>(B) )
          result = ModRefResult(result & ~Mod);

        ++numNoReverseFlow;

        // Keep track of this
        //queryAcrossCallsites(B,After,A,L);
        smtxMan->setAssumedLC(L,B,A);
      }
      else if ( slamp.isPredictableInterIterDep(L->getHeader(), A, B) )
      {
        // No flow.
        if( isa<LoadInst>(B) )
          result = ModRefResult(result & ~Ref);

        else if( isa<StoreInst>(B) )
          result = ModRefResult(result & ~Mod);

        ++numNoReverseFlow;

        slamp::PredMap predictions = slamp.getPredictions(L->getHeader(), A, B, true);
        for (slamp::PredMap::iterator i=predictions.begin(), e=predictions.end() ; i!=e ; ++i) 
        {
          LoadInst* li = i->first;

          if (i->second.type == slamp::LI_PRED)
          {
            smtxMan->setAssumedLC(L, B, li, A);
          }
          else if (i->second.type == slamp::LINEAR_PRED)
          {
            smtxMan->setAssumedLC(L, B, li, A, i->second.a, i->second.b, false);
          }
          else if (i->second.type == slamp::LINEAR_PRED_DOUBLE)
          {
            smtxMan->setAssumedLC(L, B, li, A, i->second.a, i->second.b, true);
          }
          else
          {
            assert(false);
          }
        }
      }
      else
      {
        //errs() << "--- SLAMP failed to speculate\n";
        //errs() << "    src : " ; B->dump();
        //errs() << "    dst : " ; A->dump();

        //slamp.dumpValuePredictionForEdge(L->getHeader(), A, B, true);
      }
    }

    if( result != NoModRef )
      // Chain.
      result = ModRefResult(result & LoopAA::modref(A,rel,B,L) );

    return result;
  }

}
}

