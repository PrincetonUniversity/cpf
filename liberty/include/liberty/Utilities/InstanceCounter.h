#ifndef LLVM_LIBERTY_INSTANCE_COUNTER_H
#define LLVM_LIBERTY_INSTANCE_COUNTER_H

//#define ENABLE_INSTANCE_COUNTER

#include <stdio.h>
#include <stdlib.h>
#include <typeinfo>
#include <string>

namespace liberty
{
  using namespace llvm;

#ifdef ENABLE_INSTANCE_COUNTER
  /// This class is very lightweight
  /// tool to determine if objects of a particular
  /// class are leaked, and the maximum number of them
  /// which are simultaneously live.  It is much lighter
  /// than valgrind or any dynamic binary instrumentation
  /// tool.  It is much more targetted than tools like
  /// LeakCheck, dmalloc, electric fence (arguably a
  /// feature or a bug).
  ///
  /// To use it, choose a class to examine (say class Foo).
  /// Then, simply add InstanceCounter<Foo> as a parent
  /// class of Foo.
  ///
  /// Alas, it is NOT thread safe.
  template <class Subject>
  class InstanceCounter
  {
    static bool Registered;

  public:
    static int NumLive;
    static int NumAllocations;
    static int NumDeallocations;
    static int MaxLive;
    static Subject *Last;

    Subject *Prev, *Next;

    static void printInstanceCounterStats(void)
    {
      const std::string funcName = __PRETTY_FUNCTION__;
      const size_t classStart = funcName.find_first_of('=') + 2;
      const size_t classLen = funcName.size() - classStart - 1;
      assert(classStart != std::string::npos && classLen <= funcName.size());
      const std::string className = funcName.substr(classStart, classLen);

      fprintf(stderr,
             "InstanceCounter for %s:\n"
             "   Num Allocations: %d\n"
             " Num Deallocations: %d\n"
             "    Currently live: %d\n"
             "          Max live: %d.\n\n",
             className.c_str(),
             NumAllocations, NumDeallocations,
             NumLive, MaxLive);
    }

  public:
    inline InstanceCounter() : Prev(Last), Next(0)
    {
      if( ! Registered )
      {
        // Technically, the C++ spec only guarantees
        // that the first 32 calls to atexit() will
        // succeed.
        atexit( &printInstanceCounterStats );
        Registered = true;
      }

      ++ NumAllocations;
      ++ NumLive;
      if( NumLive > MaxLive )
        MaxLive = NumLive;

      Last = static_cast< Subject* >(this);
      if( Prev )
        Prev->Next = Last;
    }

    virtual inline ~InstanceCounter()
    {
      ++ NumDeallocations;
      -- NumLive;

      if( Prev )
        Prev->Next = Next;
      if( Next )
        Next->Prev = Prev;
      if( static_cast< Subject* >(this) == Last )
        Last = Prev;

      Next = Prev = 0;

      assert( NumLive >= 0 );
    }

  };

  template <class Subject>
  bool InstanceCounter<Subject>::Registered = false;

  template <class Subject>
  int InstanceCounter<Subject>::NumAllocations = 0;

  template <class Subject>
  int InstanceCounter<Subject>::NumDeallocations = 0;

  template <class Subject>
  int InstanceCounter<Subject>::NumLive = 0;

  template <class Subject>
  int InstanceCounter<Subject>::MaxLive = 0;

  template <class Subject>
  Subject *InstanceCounter<Subject>::Last = 0;
#else

  template <class Subject>
  class InstanceCounter
  {
  public:
    inline InstanceCounter() {}
    virtual inline ~InstanceCounter() {}
  };

#endif

}

#endif
