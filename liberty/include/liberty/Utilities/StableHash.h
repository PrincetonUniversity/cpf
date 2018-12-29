// This is just like llvm's hashing algorithms, except it is designed
// to be stable across multiple executions.  In particular, this means
//  - We must hash object values, not pointer addresses.
//  - We must NOT employ the per-execution seed.
// You should expect this to be slower.

#ifndef LLVM_LIBERTY_UTILS_STABLE_HASH_H
#define LLVM_LIBERTY_UTILS_STABLE_HASH_H

#include <cassert>
#include <string>

#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"

namespace liberty
{
using namespace llvm;

typedef uint64_t stable_hash_code;

// Base case
template <class T>
stable_hash_code stable_hash(T value)
{
  assert(false && "Did not implement stable hashing of this type");
}

// Integer types
template <>
stable_hash_code stable_hash<uint64_t>(uint64_t value)
{
  // Linear congruential generator from Numerical Recipes
  return 1013904223 + 1664525*value;
}
template <> stable_hash_code stable_hash<char>(char value) { return stable_hash( (uint64_t)value ); }
template <> stable_hash_code stable_hash<int>(int value) { return stable_hash( (uint64_t)value ); }
template <> stable_hash_code stable_hash<unsigned>(unsigned value) { return stable_hash( (uint64_t)value ); }

// Pointer addresses are not repeatable;
// instead we hash the referenced object.
template <class T>
stable_hash_code stable_hash(const T *ptr)
{
  if( ptr )
  {
    const T &reference = *ptr;
    return stable_hash<const T &>(reference);
  }

  return stable_hash( (uint64_t) 0);
}

// Given two hashable types A, B,
// return a hash code for the tuple of type A*B
template <class A, class B>
stable_hash_code stable_combine(A a, B b)
{
  stable_hash_code ha = stable_hash(a),
            hb = stable_hash(b);

  return ha + stable_hash(hb);
}

template <class A, class B, class C>
stable_hash_code stable_combine(A a, B b, C c)
{
  return stable_combine(a, stable_combine(b,c));
}

template <class A, class B, class C, class D>
stable_hash_code stable_combine(A a, B b, C c, D d)
{
  return stable_combine(stable_combine(a, b), stable_combine(c,d));
}

// Iterators
template <class I>
stable_hash_code stable_hash(unsigned initial, const I &begin, const I &end)
{
  stable_hash_code acc = stable_hash(initial);
  for(I i=begin; i!=end; ++i)
    acc = stable_combine(acc, *i);
  return acc;
}

// Strings
stable_hash_code stable_hash(unsigned size, const char *data)
{
  return stable_hash(size, data, &data[size]);
}

template <>
stable_hash_code stable_hash<const std::string&>(const std::string &s)
{
  return stable_combine(s.size(), s.begin(), s.end());
}
template <>
stable_hash_code stable_hash<StringRef>(StringRef sr)
{
  return stable_hash(sr.size(), sr.data());
}

// Repeatable hash value for a function
template <>
stable_hash_code stable_hash<const Function&>(const Function &fcn)
{
  return stable_hash( fcn.getName() );
}

// Repeatable hash value for a basic block, based upon
// the parent function and the position of the block w/in that fcn.
template <>
stable_hash_code stable_hash<const BasicBlock&>(const BasicBlock &bb)
{
  const Function *fcn = bb.getParent();

  // Determine position in function
  int pos = 0;
  for(Function::const_iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i, ++pos)
    if( &bb == &*i )
      break;

  return stable_combine( fcn, pos );
}

// Repeatable hash value for an instruction, based upon
// the parent block and the position w/in that block
template <>
stable_hash_code stable_hash<const Instruction &>(const Instruction &inst)
{
  const BasicBlock *bb = inst.getParent();

  // Determine position in block
  int pos = 0;
  for(BasicBlock::const_iterator i=bb->begin(), e=bb->end(); i!=e; ++i, ++pos)
    if( &inst == &*i )
      break;

  return stable_combine( bb, pos );
}

template<>
stable_hash_code stable_hash<const GlobalVariable &>(const GlobalVariable &gv)
{
  return stable_hash( gv.getName() );
}

// Arbitrary values
template <>
stable_hash_code stable_hash<const Value &>(const Value &v)
{
  if( const Instruction *inst = dyn_cast<Instruction>(&v) )
    return stable_hash(inst);
  if( const BasicBlock *bb = dyn_cast<BasicBlock>(&v) )
    return stable_hash(bb);
  if( const Function *fcn = dyn_cast<Function>(&v) )
    return stable_hash(fcn);
  if( const GlobalVariable *gv = dyn_cast<GlobalVariable>(&v) )
    return stable_hash(gv);

  assert(false && "TODO: implement this sub-type of value");
}

}

#endif

