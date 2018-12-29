#ifndef MEMORY_SPACE_ASSUMPTIONS_H
#define MEMORY_SPACE_ASSUMPTIONS_H

#include "typedefs.h"
#include "liberty/Exclusions/Exclusions.h"
#include "liberty/Target/QueueLibrary.h"

namespace liberty {
  namespace msa {

    using namespace llvm;

    class ModuleHandle {
      public:
        virtual ~ModuleHandle() {}


        // Handle live-in values; i.e. somehow distribute these
        // values to all threads that use them, across
        // thread (and potentially memory) boundaries.
        //  - values is a set of live-in values
        //    that exist within the original function.
        //  - entries0 is the set of basic blocks in
        //    the zeroth thread at the entry to the
        //    partitioned region
        //  - threadEntry is the entry basic block in some
        //    thread t>0
        //  - replication is the replication factor for this
        //    thread; sequential stages get 1, parallel stages
        //    get some n>1
        //  - dynReplicantId is a value that, at run time will
        //    hold the replicant id number s.t. 0 <= Id < n.
        //    It must be accessible in threadEntry.
        //    This is only relevant for parallel stages.
        virtual
        void handleLiveIns(const ValueList &values,
                           const BBSet &entries0,
                           BasicBlock *threadEntry,
                           unsigned replication = 1,
                           Value *dynReplicantId = 0) = 0;

        // Handle live-out values; i.e. somehow collect the final
        // version of these values from all of the worker threads,
        // and get them to the zeroth thread.
        //  - values is a set of live-out values
        //    for this thread t>0
        //  - threadExit is a set of exit block from
        //    this thread t>0
        //  - exits0 is the set of exit points
        //    of the zeroth thread.
        //  - replication is the replication factor for
        //    this thread; sequential stages get 1,
        //    parallel stages  get some n>1
        //  - dynReplicantId is a value that, at run time
        //    will hold the replicant id number: 0 <= Id < n.
        //    It must be accessible in all of the threadExits.
        //    This is only relevant to parallel stages.
        //  - dynTimestamp is a value that at run time will
        //    hold a monotonically increasing time value.
        //    Time > 0.  It must be accessible in all of the
        //    thread exits.  This is only relevant for parallel
        //    stages.
        //
        //  - values0 is an output argument: it is the GlobalVariable that will
        //  hold the live-outs in thread 0.  Note that even if we are using
        //  queues rather than shared memory, the live-outs should be stored to
        //  this GlobalVariable to support reductions.
        virtual
        Value * handleLiveOuts(const ValueList &valueList,
                            const BBSet &threadExits,
                            const BBSet &exits0,
                            unsigned replication = 1,
                            Value *dynReplicantId = 0) = 0;

        // Handle synchronization of memory dependencies
        // between threads.  m1 is a memory-access instruction
        // in thread t, and m2 is a memory-access instruction
        // in thread u.  Alias analysis suggests these two
        // accesses may alias.
        virtual
        void synchronizeMemoryDependence(Instruction *m1,
                                         Instruction *m2) = 0;


        // Forward stores to a commit thread if necessary.
        virtual void forwardStore(StoreInst *store, ThreadNo myThread,
          ThreadNo commitThread, BasicBlock *header, BasicBlock *sync) {
        }
    };



    // And interface to generalize the assumptions
    // of the various memory models we use.
    //
    // Compare to ThreadingLibrary
    //
    // Assumptions governing how memory dependences
    // between threads are resolved.  In particular,
    // this affects the handling of:
    //  (1) Live-in variables
    //  (2) Live-out variables
    //  (3) Memory synchronization (token vs. value forwarding)
    //
    class MemorySpaceAssumptions : public ImmutablePass {
      public:


        static char ID;
        MemorySpaceAssumptions() : ImmutablePass(ID) {}
        MemorySpaceAssumptions(char &id) : ImmutablePass(id) {}
        virtual ~MemorySpaceAssumptions() {}

        virtual ModuleHandle *open(Module *m) = 0;
        virtual void close(ModuleHandle *mh) = 0;

        // Estimated cost of transferring one livein/liveout.
        virtual uint64_t estimateLiveInCost() const = 0;
        virtual uint64_t estimateLiveOutCost() const = 0;
    };
  }
}

#endif
