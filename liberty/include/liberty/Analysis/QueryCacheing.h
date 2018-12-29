#ifndef LLVM_LIBERTY_QUERY_CACHEING
#define LLVM_LIBERTY_QUERY_CACHEING

// Defines several types which are used
// as hash-table keys for various query types.

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ClassicLoopAA.h"

namespace liberty
{
  struct IIKey
  {
    IIKey(const Instruction *inst1, LoopAA::TemporalRelation r, const Instruction *inst2, const Loop *loop)
      : i1(inst1), Rel(r), i2(inst2), L(loop) {}
    const Instruction *i1;
    LoopAA::TemporalRelation Rel;
    const Instruction *i2;
    const Loop *L;
  };

  struct InstFcnKey
  {
    InstFcnKey(const Instruction *inst, LoopAA::TemporalRelation r, const Function *fcn, const Loop *loop)
      : i1(inst), Rel(r), f(fcn), L(loop) {}
    const Instruction *i1;
    LoopAA::TemporalRelation Rel;
    const Function *f;
    const Loop *L;
  };

  struct FcnInstKey
  {
    FcnInstKey(const Function *fcn, LoopAA::TemporalRelation r, const Instruction *inst, const Loop *loop)
      : f(fcn), Rel(r), i2(inst), L(loop) {}
    const Function *f;
    LoopAA::TemporalRelation Rel;
    const Instruction *i2;
    const Loop *L;
  };

  struct FcnPtrKey
  {
    FcnPtrKey(const Function *fcn, LoopAA::TemporalRelation r, const Value *ptr, unsigned size, const Loop *loop)
      : f(fcn), Rel(r), p2(ptr), s2(size), L(loop) {}
    const Function *f;
    LoopAA::TemporalRelation Rel;
    const Value *p2;
    unsigned s2;
    const Loop *L;
  };

  struct PtrPtrKey
  {
    PtrPtrKey(const Value *P1, unsigned S1, LoopAA::TemporalRelation r, const Value * P2, unsigned S2, const Loop *loop)
      : p1(P1), s1(S1), Rel(r), p2(P2), s2(S2), L(loop) {}

    PtrPtrKey(const ClassicLoopAA::Pointer &P1, LoopAA::TemporalRelation r, const ClassicLoopAA::Pointer &P2, const Loop *loop)
      : p1(P1.ptr), s1(P1.size), Rel(r), p2(P2.ptr), s2(P2.size), L(loop) {}

    const Value *p1;
    unsigned s1;
    LoopAA::TemporalRelation Rel;
    const Value *p2;
    unsigned s2;
    const Loop *L;
  };

}

namespace llvm
{
  using namespace liberty;

  template <> struct isPodLike< IIKey > { static const bool value = true; };
  template <> struct isPodLike< InstFcnKey > { static const bool value = true; };
  template <> struct isPodLike< FcnInstKey > { static const bool value = true; };
  template <> struct isPodLike< FcnPtrKey > { static const bool value = true; };
  template <> struct isPodLike< PtrPtrKey > { static const bool value = true; };

  template <>
  struct DenseMapInfo< IIKey >
  {
    static inline IIKey getEmptyKey()
    {
      return IIKey(0,LoopAA::Same,0,0);
    }

    static inline IIKey getTombstoneKey()
    {
      return IIKey(0,LoopAA::Before,0,0);
    }

    static unsigned X(unsigned a, unsigned b)
    {
      std::pair< unsigned, unsigned > pair(a,b);
      return DenseMapInfo< std::pair< unsigned, unsigned > >::getHashValue(pair);
    }

    static unsigned getHashValue(const IIKey &value)
    {
      return
      X(DenseMapInfo<const Instruction *>::getHashValue(value.i1),
      X(DenseMapInfo<unsigned>::getHashValue(value.Rel),
      X(DenseMapInfo<const Instruction *>::getHashValue(value.i2),
        DenseMapInfo<const Loop *>::getHashValue(value.L))));
      ;
    }

    static bool isEqual(const IIKey &a, const IIKey &b)
    {
      return a.i1 == b.i1
      &&     a.Rel == b.Rel
      &&     a.i2 == b.i2
      &&     a.L == b.L;
    }
  };

  template <>
  struct DenseMapInfo< InstFcnKey >
  {
    static inline InstFcnKey getEmptyKey()
    {
      return InstFcnKey(0,LoopAA::Same,0,0);
    }

    static inline InstFcnKey getTombstoneKey()
    {
      return InstFcnKey(0,LoopAA::Before,0,0);
    }

    static unsigned X(unsigned a, unsigned b)
    {
      std::pair< unsigned, unsigned > pair(a,b);
      return DenseMapInfo< std::pair< unsigned, unsigned > >::getHashValue(pair);
    }

    static unsigned getHashValue(const InstFcnKey &value)
    {
      return
      X(DenseMapInfo<const Instruction *>::getHashValue(value.i1),
      X(DenseMapInfo<unsigned>::getHashValue(value.Rel),
      X(DenseMapInfo<const Function *>::getHashValue(value.f),
        DenseMapInfo<const Loop *>::getHashValue(value.L))));
      ;
    }

    static bool isEqual(const InstFcnKey &a, const InstFcnKey &b)
    {
      return a.i1 == b.i1
      &&     a.Rel == b.Rel
      &&     a.f == b.f
      &&     a.L == b.L;
    }
  };

  template <>
  struct DenseMapInfo< FcnInstKey >
  {
    static inline FcnInstKey getEmptyKey()
    {
      return FcnInstKey(0,LoopAA::Same,0,0);
    }

    static inline FcnInstKey getTombstoneKey()
    {
      return FcnInstKey(0,LoopAA::Before,0,0);
    }

    static unsigned X(unsigned a, unsigned b)
    {
      std::pair< unsigned, unsigned > pair(a,b);
      return DenseMapInfo< std::pair< unsigned, unsigned > >::getHashValue(pair);
    }

    static unsigned getHashValue(const FcnInstKey &value)
    {
      return
      X(DenseMapInfo<const Function *>::getHashValue(value.f),
      X(DenseMapInfo<unsigned>::getHashValue(value.Rel),
      X(DenseMapInfo<const Instruction *>::getHashValue(value.i2),
        DenseMapInfo<const Loop *>::getHashValue(value.L))));
      ;
    }

    static bool isEqual(const FcnInstKey &a, const FcnInstKey &b)
    {
      return a.f == b.f
      &&     a.Rel == b.Rel
      &&     a.i2 == b.i2
      &&     a.L == b.L;
    }
  };

  template <>
  struct DenseMapInfo< FcnPtrKey >
  {
    static inline FcnPtrKey getEmptyKey()
    {
      return FcnPtrKey(0,LoopAA::Same,0,0,0);
    }

    static inline FcnPtrKey getTombstoneKey()
    {
      return FcnPtrKey(0,LoopAA::Before,0,0,0);
    }

    static unsigned X(unsigned a, unsigned b)
    {
      std::pair< unsigned, unsigned > pair(a,b);
      return DenseMapInfo< std::pair< unsigned, unsigned > >::getHashValue(pair);
    }

    static unsigned getHashValue(const FcnPtrKey &value)
    {
      return
      X(DenseMapInfo<const Function *>::getHashValue(value.f),
      X(DenseMapInfo<unsigned>::getHashValue(value.Rel),
      X(DenseMapInfo<const Value *>::getHashValue(value.p2),
      X(DenseMapInfo<unsigned>::getHashValue(value.s2),
        DenseMapInfo<const Loop *>::getHashValue(value.L)))));
      ;
    }

    static bool isEqual(const FcnPtrKey &a, const FcnPtrKey &b)
    {
      return a.f == b.f
      &&     a.Rel == b.Rel
      &&     a.p2 == b.p2
      &&     a.s2 == b.s2
      &&     a.L == b.L;
    }
  };

  template <>
  struct DenseMapInfo< PtrPtrKey >
  {
    static inline PtrPtrKey getEmptyKey()
    {
      return PtrPtrKey(0,0,LoopAA::Same,0,0,0);
    }

    static inline PtrPtrKey getTombstoneKey()
    {
      return PtrPtrKey(0,0,LoopAA::Before,0,0,0);
    }

    static unsigned X(unsigned a, unsigned b)
    {
      std::pair< unsigned, unsigned > pair(a,b);
      return DenseMapInfo< std::pair< unsigned, unsigned > >::getHashValue(pair);
    }

    static unsigned getHashValue(const PtrPtrKey &value)
    {
      return
      X(DenseMapInfo<const Value *>::getHashValue(value.p1),
      X(DenseMapInfo<unsigned>::getHashValue(value.s1),
      X(DenseMapInfo<unsigned>::getHashValue(value.Rel),
      X(DenseMapInfo<const Value *>::getHashValue(value.p2),
      X(DenseMapInfo<unsigned>::getHashValue(value.s2),
        DenseMapInfo<const Loop *>::getHashValue(value.L))))));
      ;
    }

    static bool isEqual(const PtrPtrKey &a, const PtrPtrKey &b)
    {
      return a.p1 == b.p1
      &&     a.s1 == b.s1
      &&     a.Rel == b.Rel
      &&     a.p2 == b.p2
      &&     a.s2 == b.s2
      &&     a.L == b.L;
    }
  };

}

#endif

