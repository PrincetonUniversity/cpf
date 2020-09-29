#ifndef QUEUE_LIBRARY_H
#define QUEUE_LIBRARY_H

/* QueueLibrary.h
 *
 * A layer of abstraction around whatever queue library we are
 * using at the moment.  We seem to have many queue libraries
 * that change frequently, so the need for this has become
 * a reality.
 *
 * This is intended to be as general as need be, but no more
 * so.  Its generality is expected to increase monotonically.
 *
 * These implement an AnalysisGroup.  Why?
 *
 *  - Because there are multiple implementations of
 *    the queue library, each with different capabilities
 *    and which require different support code.
 *
 *  - Because certain passes (MTCG) depend upon this
 *    functionality, yet we want to decouple it from
 *    the MTCG algorithm.
 *
 *  - Because as an analysis group, we can select
 *    a particular queue implementation from
 *    the commandline (e.g. -hanjun).
 *    We can decide at run time, and do not need to
 *    decide at compile time.
 *
 *  - Because every five minutes or so, the Liberty group
 *    invents a new variant of (or at least a new interface
 *    to) the queue.
 */

#include <map>
#include <utility>
#include <sstream>
#include <string>

#include "llvm/IR/BasicBlock.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/ADT/DenseMap.h"

#include "liberty/Utilities/InstInsertPt.h"


namespace liberty {
  namespace ql {

    using namespace llvm;

    // Abstract handle to some queue.
    class QueueNo {
      public:
        virtual ~QueueNo() {}

        // Create a produce-consume pair.
        // The produce instruction is inserted at src, the consume is inserted at dst.
        // The value srcVal is transmitted over the queue.
        // Unlike the previous, this uses the supplied queue.
        // Returns the instruction which computes the value of the consume.
        Instruction *transfer(InstInsertPt &src, Value *srcVal, InstInsertPt &dst);

        // Create a produce instruction at src which sends the value srcVal
        // over the queue q.  This call should be coupled by a call
        // to transferTo.
        virtual void transferFrom(InstInsertPt &src, Value *srcVal) = 0;

        // Create a consume instruction at dst from queue q.
        // The received value will have the same type
        // as srcVal.
        // Returns the instruction which computes the value of the consume.
        virtual Instruction *transferTo(Value *srcVal, InstInsertPt &dst) = 0;

        // Create a sequence of instructions to flush a queue.
        // Some implementations of queues will buffer their
        // sequences into blocks, and this ensures that the
        // last block will be sent.
        virtual void flush(InstInsertPt &dst) = 0;
    };

    // A queue that supports many-to-one  communications
    class ManyToOneQueue : public QueueNo {
      public:
        // Everything supported by the 1-1 queues,
        // and also:

        // Allow this loop to be rotated at least once
        // per iteration.  <dst> is a point within
        // the destination (one) function.
        virtual void rotate(InstInsertPt &dst) = 0;
    };

    // A queue that supports one-to-many communications
    class OneToManyQueue : public QueueNo {
      public:
        // Everything supported by the 1-1 queues,
        // and also:

        // Allow this loop to be rotated at least once
        // per iteration. <src> is a point within
        // the source (single) function.
        virtual void rotate(InstInsertPt &src, InstInsertPts &initPoints) = 0;

        // Transfer to all consumers, not only one
        virtual void transferToAllFrom(InstInsertPt &src, Value *srcVal) = 0;

        Instruction *transferToAll(InstInsertPt &src, Value *srcVal, InstInsertPt &dst) {
          transferToAllFrom(src,srcVal);
          return transferTo(srcVal,dst);
        }
    };


    // Encapsulate's all of a queue library's
    // activities within a particular module.
    // This was introduced so that a queue
    // library can be attached to more
    // than one module at a time, allowing
    // us to put produces in one module and
    // consumes into another.
    class ModuleHandle {
      public:
        virtual ~ModuleHandle() {}

        // Operations supported by all queue libraries

        // Allocate and initialize a new queue.
        //
        // The src and dst parameters allow the queue
        // library to coalesce many queues when control
        // flow allows this.  If coalescing is to be performed,
        // the queue library must be careful to ensure that
        // produces and consumes take place in the same order.
        //
        // Storage location is a constant expression representing
        // a place to put the queue.  It must be of type
        // pointer to getStorageType().  If null, the queue library
        // will allocate one automatically.  This allows for
        // fine-grained control over allocation.
        virtual QueueNo *allocateQueue() = 0;
        virtual QueueNo *allocateQueue(unsigned, unsigned) = 0;
        virtual OneToManyQueue *allocateOneToManyQueue( unsigned replication, Value *dynTimestamp ) = 0;
        virtual ManyToOneQueue *allocateManyToOneQueue( unsigned replication, Value *dynTimestamp ) = 0;
        virtual void releaseQueue(QueueNo *qn) = 0;

        // Create a produce-consume pair.
        // This is a common case, where the queue is used to
        // produce/consume *exactly one* datum.
        //
        // The produce instruction is inserted at src, the consume is inserted at dst.
        //
        // The value srcVal is transmitted over the queue.
        //
        // It returns the value at the consume point.
        //
        // A queue is allocated automatically, and is only used for this
        // produce-consume pair.
        // Returns the instruction which computes the value of the consume.
        Instruction *transfer(
          InstInsertPt &src,
          Value *srcVal,
          InstInsertPt &dst);
    };



    // Abstract Interface to a QueueLibrary
    //
    // A queue library provides the transfer, transferFrom and transferTo
    // methods, which add produce/consume instructions to a function.
    // They may do other things, such as add global variables and static
    // constructor/destructor calls.
    //
    //
    // A QueueLibary::QueueNo represents a particular queue created by
    // a QueueLibrary in the target module.  QueueNo's mean different things
    // depending upon the implementation, and thus they are abstract here.
    // Each implementation creates a queue handle which derives from
    // QueueLibrary::QueueNo so C++ typing is satisfied.
    class QueueLibrary : public ImmutablePass {
      public:
        // llvm pass id
        static char ID;
        QueueLibrary() : ImmutablePass(ID) {}
        QueueLibrary(char &id) : ImmutablePass(id) {}
        virtual ~QueueLibrary() {}

        // Open a module
        virtual ModuleHandle *open(Module *m) = 0;
        virtual void close(ModuleHandle *qh) = 0;

        // Returns the minimum number of transfers
        // before a single queue of this type
        // becomes efficient.  The queue will
        // still work when fewer than this many
        // transfers are performed, but less
        // efficiently.
        virtual size_t minEfficientTransfers() const = 0;

        virtual uint64_t estimateProduceCost() const = 0;
        virtual uint64_t estimateConsumeCost() const = 0;
    };
  }
}

#endif
