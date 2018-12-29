#ifndef LLVM_LIBERTY_FOLD_H
#define LLVM_LIBERTY_FOLD_H

#include <cstdlib>

namespace liberty
{

using namespace llvm;

/// The foldl function for an arbitrarty
/// iterator type.
template <class Reduction>
struct Fold
{
  template <class InputIterator>
  static typename Reduction::Type left( InputIterator begin, InputIterator end)
  {
    typename Reduction::Type result = Reduction::Identity;

    InputIterator i=begin;
    if( i == end )
      return result;
    result = *i;
    ++i;

    for(; i!=end; ++i)
      result = Reduction::reduce(result, *i);

    return result;
  }
};


}

#endif // LLVM_LIBERTY_FOLD_H

