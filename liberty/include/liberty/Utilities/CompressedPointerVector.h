#ifndef LLVM_LIBERTY_COMPRESSED_POINTER_VECTOR_H
#define LLVM_LIBERTY_COMPRESSED_POINTER_VECTOR_H

#include <assert.h>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <iostream>

namespace liberty
{
  /// A somewhat less-ugly way to
  /// define an integer which is
  /// big-enough to hold a pointer.
  template <unsigned PtrSize>
  struct BigEnough
  { };

  template <>
  struct BigEnough<4U>
  {
    typedef uint32_t    Integer;
  };

  template <>
  struct BigEnough<8U>
  {
    typedef uint64_t    Integer;
  };

  template <class Type> class CompressedPointerVector;
  template <class Type> class CompressedPointerVectorBuilder;


  /// Allows forward iteration over the elements
  /// of a CompressedPointerVector
  template <class Type>
  class CompressedPointerVectorIterator
  {
  public:
    typedef typename BigEnough<sizeof(Type*)>::Integer Int;
    typedef typename CompressedPointerVector<Type>::Storage::const_iterator StorageIterator;
    typedef CompressedPointerVectorIterator<Type> iterator;

    CompressedPointerVectorIterator(const StorageIterator &i, const StorageIterator &e, Int initial=0U)
      : iter(i), end(e), accumulator(initial)
    {
      ++*this;
    }

    /// Dereference the iterator
    Type * operator*() const
    {
      return (Type*)accumulator;
    }

    /// Advance the iterator
    iterator &operator++()
    {
      if( iter == end )
        return *this;

      uint8_t byte = *iter;
      ++iter;

      // Is this a delta or a sync?
      if( (byte & 1U) == 1U )
      {
        // Sync!

        // this marks a full entry, remove
        // the sentinel bit.
        Int full = (Int) (byte & ~1U);

        // read more the remaining sizeof(Int)-1 bytes...
        // Remember, least significant first.
        for( unsigned i=1U, shift=8U; i<sizeof(Int)/sizeof(uint8_t); ++i, shift += 8U, ++iter)
          full |= ( (Int) *iter ) << shift;

        accumulator = full;
      }
      else
      {
        // Delta!

        accumulator += byte;
      }

      return *this;
    }

    /// Compare for inequality.
    bool operator!=(const iterator &other) const
    {
      return iter != other.iter;
    }

  private:
    StorageIterator iter, end;
    Int accumulator;
  };

  /// This class is a space-efficient
  /// method of storing a large number of
  /// pointers, effectively using 14-bits
  /// per pointer.
  ///
  /// The trick is that it will store
  /// the difference between pointer
  /// values.  If you sort the list first,
  /// It turns out that a large fraction
  /// of these values are within 256 of their
  /// neighbor.
  ///
  /// It assumes that the pointers are
  /// aligned to even addresses (i.e.
  /// the low bit of a pointer is
  /// 0).  This means it will NOT
  /// work on char*.
  ///
  /// As long as the differences are less than 2**8,
  /// we can store it in a single byte.  All
  /// other values we store as native-sized
  /// pointers.
  ///
  /// And if you think this is an unreasonable
  /// hack, I'll remind you that llvm::Value
  /// does something like this for it's use_list:
  /// http://llvm.org/docs/ProgrammersManual.html#UserLayout
  template <class Type>
  class CompressedPointerVector
  {
    /// don't call this
    CompressedPointerVector(const CompressedPointerVector<Type> &) { assert(false); }
    /// don't call this
    CompressedPointerVector<Type> &operator=(const CompressedPointerVector<Type> &) { assert(false); return *this; }
  public:
    typedef typename BigEnough<sizeof(Type*)>::Integer Int;
    typedef std::vector<uint8_t> Storage;
    typedef CompressedPointerVectorIterator<Type> iterator;

    CompressedPointerVector() : sz(0U), bytes() {}

    /// Determine the number of elements in this collection.
    unsigned size() const { return sz-1; }

    /// Get a forward iterator to all elements
    /// of this collection.
    iterator begin() const
    {
      Storage::const_iterator b=bytes.begin(), e=bytes.end();
      return iterator(b,e);
    }

    /// Get the end iterator corresponding to begin()
    iterator end() const
    {
      Storage::const_iterator e=bytes.end();
      return iterator(e,e);
    }

    /// Returns the effective number of bits required
    /// for each pointer.
    double efficiency() const
    {
      return 8.0 * bytes.size() / size();
    }

  private:
    unsigned sz;
    Storage bytes;

  protected:
    friend class CompressedPointerVectorBuilder<Type>;

    void reserve(unsigned n)
    {
      bytes.reserve(n);
    }

    void push_diff(uint8_t delta)
    {
      assert( (delta & 1U) == 0U && "Alignment assumption was incorrect for this type!");

      ++sz;
      bytes.push_back(delta);
    }

    void push_sync(Int full)
    {
      assert( (full & 1U) == 0U && "Alignment assumption was incorrect for this type!");

      ++sz;

      // Mark this as a full sync entry
      // by making least significant bit 1.
      // This is sound if (1) we store the
      // sync records in little-endian order,
      // and (2) our pointer types have alignment
      // of AT LEAST 2 bytes.
      full |= 1U;

      // Write the integer byte-by-byte; write
      // least-significant first.
      for(unsigned i=0U; i< sizeof(Int)/sizeof(uint8_t); ++i, full >>= 8U)
        bytes.push_back( (uint8_t) (full & 0xff));
    }

  };

  /// This is used to construct a
  /// CompressedPointerVector.  You build
  /// one by push()ing zero or more elements,
  /// and then calling the distill() method.
  /// After they have been distilled, a
  /// CompressedPointerVector should be treated
  /// as if it is immutable.
  template <class Type>
  class CompressedPointerVectorBuilder
  {
// Average number of bits required for one pointer
// Any number >0 will work, but better
// estimates yield faster distill()ation.
#define AVERAGE_EFFICIENCY      (14)

  public:
    typedef typename BigEnough<sizeof(Type*)>::Integer Int;
    typedef std::vector<Type *> Storage;
    typedef typename Storage::iterator   iterator;
    typedef typename Storage::const_iterator const_iterator;

  private:
    Storage pointers;

  public:
    CompressedPointerVectorBuilder() : pointers() {}

    /// Add a new pointer to the collection
    void push(Type *t)
    {
      pointers.push_back(t);
    }

    /// Remove a pointer from the collection
    void remove(Type *t)
    {
      iterator i = std::find(pointers.begin(), pointers.end(), t);
      if( i == pointers.end() )
        return;

      *i = pointers.back();
      pointers.pop_back();
    }

    /// You can iterate over the collection
    /// before you distill, if you wish
    const_iterator begin() const { return pointers.begin(); }
    const_iterator end() const { return pointers.end(); }


    /// Release internal storage
    void clear()
    {
      pointers.clear();
    }

    /// Construct a compressed pointer vector
    /// from the elements which have been added
    /// so far.
    void distill(CompressedPointerVector<Type> &output)
    {
      std::sort( pointers.begin(), pointers.end() );

      const unsigned expectedSize = AVERAGE_EFFICIENCY * pointers.size() / 8;
      output.reserve( expectedSize );

      Int prev = 0U;
      for(iterator i=pointers.begin(), e=pointers.end(); i!=e; ++i)
      {
        const Int next = (Int) *i;
        const Int diff64 = next - prev;
        const uint8_t  diff8  = (uint8_t) (diff64 & 0xffU);

        if( diff64 == diff8 )
          output.push_diff( diff8 );
        else
          output.push_sync( next );

        prev = next;
      }

      // put one more to make the iterator easier to implement
      output.push_diff(0);

      assert( output.size() == pointers.size() );
    }
  };


}
#endif


