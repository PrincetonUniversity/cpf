#ifndef LLVM_LIBERTY_TWO_LIST_H
#define LLVM_LIBERTY_TWO_LIST_H

#include "llvm/ADT/SmallVector.h"

namespace liberty
{
  /// A compact representation of two lists 'A' and 'Z'.
  /// This is useful for storing two adjacency lists
  /// within graphs (Lattice, PDG, etc).
  ///
  /// StorageTy must be a random access,
  /// back insertion sequence.  Said another
  /// way, it must support the operator[](unsigned),
  /// insert(iter,elt) and push_back().  Both
  /// llvm::SmallVector<> and std::vector<> work.
  ///
  /// The first list is represented as elements
  /// 0 -- a_size-1, and the second list is
  /// represented as elements a_size -- n-1.
  /// If a_size == 0, then the first list is empty;
  /// If a_size == n, then the second list is empty.
  template <class EltTy, class StorageTy>
  class TwoList
  {
    StorageTy         storage;
    unsigned          a_size;

  public:
    typedef typename StorageTy::const_iterator iterator;

    TwoList() : storage(), a_size(0U) {}

    /// Add the element elt to the A list.
    void push_a(EltTy elt)
    {
      storage.insert( storage.begin(), elt);
      ++ a_size;
    }

    /// Determine the number of elements in
    /// the A list.
    unsigned size_a() const
    {
      return a_size;
    }

    /// Get a forward iterator for the A list.
    iterator begin_a() const
    {
      if( a_size < 1 )
        return storage.end();
      else
        return storage.begin();
    }

    /// Get the end iterator corresponding to begin_a()
    iterator end_a() const
    {
      if( a_size < 1 )
        return storage.end();
      else if( a_size >= storage.size() )
        return storage.end();
      else
        return & storage[ a_size ];
    }

    /// Get the i-th item from the A list
    EltTy &idx_a(unsigned i)
    {
      assert( i < size_a() );
      return storage[i];
    }

    /// Add the element elt to the Z list
    void push_z(EltTy elt)
    {
      storage.push_back(elt);
    }

    /// Determine the number of elements in
    /// the 'Z' list.
    unsigned size_z() const
    {
      return storage.size() - a_size;
    }

    /// Get a forward iterator for the Z list.
    iterator begin_z() const
    {
      if( a_size >= storage.size() )
        return storage.end();
      else
        return & storage[ a_size ];
    }

    /// Get the end iterator corresponding to begin_z()
    iterator end_z() const
    {
      return storage.end();
    }

    /// Get the i-th item from the Z list
    EltTy &idx_z(unsigned i)
    {
      assert( i < size_z() );
      return storage[a_size + i];
    }


  };

}

#endif

