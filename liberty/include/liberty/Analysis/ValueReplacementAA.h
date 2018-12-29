#ifndef LLVM_LIBERTY_VALUE_REPLACEMENT_AA
#define LLVM_LIBERTY_VALUE_REPLACEMENT_AA

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Utilities/GetMemOper.h"
#include "liberty/Utilities/GetSize.h"

namespace liberty
{
using namespace llvm;

/// Temporarily add this to the LoopAA stack
/// as to represent specific, local information about
/// value equivalence.
class ValueReplacementAA : public LoopAA
{
    DenseMap<const Value*, const Value*> replacement;

  public:
    ValueReplacementAA() : LoopAA() {}

    void replace(const Value *formal, Value *actual)
    {
      replacement[formal] = actual;
    }

    SchedulingPreference getSchedulingPreference() const { return SchedulingPreference(Top+1); }

    StringRef getLoopAAName() const { return "value-replacement-aa"; }

    AliasResult alias(
      const Value *ptrA, unsigned sizeA,
      TemporalRelation rel,
      const Value *ptrB, unsigned sizeB,
      const Loop *L)
    {
      if( replacement.count(ptrA) )
        ptrA = replacement[ptrA];
      if( replacement.count(ptrB) )
        ptrB = replacement[ptrB];

      return LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L);
    }

    ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Value *ptrB, unsigned sizeB,
      const Loop *L)
    {
      if( replacement.count(ptrB) )
        ptrB = replacement[ptrB];

      if( const Value *ptrA = getMemOper(A) )
      {
        if( replacement.count(ptrA) )
        {
          ptrA = replacement[ptrA];
          unsigned sizeA = getTargetSize(ptrA,getDataLayout());
          if( LoopAA::alias(ptrA,sizeA,rel,ptrB,sizeB,L) == NoAlias )
            return NoModRef;
        }
      }

      return LoopAA::modref(A,rel,ptrB,sizeB,L);
    }

    ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Instruction *B,
      const Loop *L)
    {
      if( const Value *ptrA = getMemOper(A) )
      {
        if( replacement.count(ptrA) )
        {
          ptrA = replacement[ptrA];
          unsigned sizeA = getTargetSize(ptrA, getDataLayout());

          const Value *ptrB = getMemOper(B);
          if( ptrB && replacement.count(ptrB) )
          {
            ptrB = replacement[ptrB];
            unsigned sizeB = getTargetSize(ptrB, getDataLayout());

            // (ptr,ptr)
            if( LoopAA::alias(ptrA,sizeA, rel, ptrB,sizeB, L) == NoAlias )
              return NoModRef;
          }
          else
          {
            // (ptr,inst)
            ModRefResult inverse = LoopAA::modref(B,Rev(rel),ptrA,sizeA,L);
            if( inverse == NoModRef )
              return NoModRef;
          }
        }
      }

      if( const Value *ptrB = getMemOper(B) )
      {
        if( replacement.count(ptrB) )
        {
          ptrB = replacement[ptrB];
          unsigned sizeB = getTargetSize(ptrB, getDataLayout());

          // (inst,ptr)

          return LoopAA::modref(A,rel,ptrB,sizeB,L);
        }
      }

      // (inst,inst)
      return LoopAA::modref(A,rel,B,L);
    }

    bool pointsToConstantMemory(const Value *P, const Loop *L)
    {
      if( replacement.count(P) )
        P = replacement[P];

      return LoopAA::pointsToConstantMemory(P,L);
    }

};

}

#endif // LLVM_LIBERTY_VALUE_REPLACEMENT_AA

