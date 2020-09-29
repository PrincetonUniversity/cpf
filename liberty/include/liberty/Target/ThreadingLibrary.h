#ifndef THREADING_LIBRARY_H
#define THREADING_LIBRARY_H

#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Target/MemorySpaceAssumptions.h"
#include "liberty/Target/QueueLibrary.h"

namespace liberty {
  namespace tl {

    using namespace llvm;

    // Represents a succinct ``handle'' to a spawned
    // thread.  It is introduced
    // by ThreadPoolHandle::allocateThread() and eliminated by
    // ThreadPoolHandle::releaseThread().
    //
    // Think of it as analogous to a pthread_t
    //
    // We leave it abstract here so that any
    // concrete ThreadingLibrary may define one
    // to suit their own needs.
    class ThreadHandle {
      public:
        virtual ~ThreadHandle() {}

        // Cause some work to take place in this thread.
        // Value should in scope at the insertion point

        // When there are several entry points to the
        // multithreaded region.  The iterators are
        // iterators of basic blocks
        template <class InputIterator>
        void spawn(Value *arg, InputIterator entryPtsBegin, InputIterator entryPtsEnd) {
          for(InputIterator i=entryPtsBegin; i!=entryPtsEnd; ++i) {
            InstInsertPt b = InstInsertPt::Beginning(*i);
            spawn(arg, b );
          }
        }

        // When there are several exit points
        template <class InputIterator>
        void join(InputIterator exitPtsBegin, InputIterator exitPtsEnd) {
          for(InputIterator i=exitPtsBegin; i!=exitPtsEnd; ++i) {
            InstInsertPt b = InstInsertPt::End(*i);
            join( b );
          }
        }

  template <class InputIterator>
  void joinBefore(InputIterator exitPtsBegin, InputIterator exitPtsEnd) {
    for (InputIterator i = exitPtsBegin; i !=exitPtsEnd; ++i) {
      InstInsertPt insPt = InstInsertPt::Before(*i);
      join(insPt);
    }
        }


        // When there is a single entry point to the
        // multithreaded region
        virtual void spawn(Value *arg, InstInsertPt &entryPt) = 0;

        // When there is a single exit point from the
        // multithreaded region
        virtual void join(InstInsertPt &exitPt) = 0;
    };

    // Represents a pool of threads.
    // It is introduced by ModuleHandle::allocateThreadPool()
    // and eliminated by ModuleHandle::releaseThreadPool().
    //
    // We leave it abstract here to that any
    // concreate ThreadingLibrary may define one
    // to suit their needs.
    class ThreadPoolHandle {
      public:
        virtual ~ThreadPoolHandle() {}

        // Allocate/release a thread handle
        // This accomodates the not-uncommon case
        // that a single thread must be spawned/joined
        // in many distinct locations
        virtual ThreadHandle *allocateThread(Function *f) = 0;
        virtual void releaseThread(ThreadHandle *th) = 0;
    };

    // Represents opening a module for writing.
    // Can be used to create thread pools within
    // a module.
    //
    // It is introduced by ThreadingLibrary::open()
    // and eliminated by ThreadingLibrary::close().
    class ModuleHandle {
      public:
        virtual ~ModuleHandle() {}

      // Create a new thread pool, given the
      // minimum, expected, and maximum number
      // of threads.  For each parameter, a
      // value of 0 means no-value-provided.
      // Give it as much info as you have available,
      // though there is no guarantee that the
      // threading library will take advantage of
      // it.
      virtual ThreadPoolHandle *initializeThreadPool(
        size_t minThreads = 0,
        size_t expectThreads = 0,
        size_t maxThreads = 0) = 0;

      // Signify that no further operations will be
      // perfomed on this thread pool.
      virtual void releaseThreadPool(ThreadPoolHandle *tph) = 0;
    };



    // Abstracts-away the various implementations
    // of thread pools.
    class ThreadingLibrary : public ImmutablePass {
      public:
        static char ID;

        ThreadingLibrary() : ImmutablePass(ID) {}
        ThreadingLibrary(char &id) : ImmutablePass(id) {}
        virtual ~ThreadingLibrary() {}

        // Open this module for writing
        virtual ModuleHandle *open(Module *m) = 0;
        virtual void close(ModuleHandle *mh) = 0;

        // Estimated cost of starting/stopping one thread.
        virtual uint64_t estimateSpawnCost() const = 0;
        virtual uint64_t estimateJoinCost() const = 0;
    };

  }
}

#endif

