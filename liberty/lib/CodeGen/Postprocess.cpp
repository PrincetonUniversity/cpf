#define DEBUG_TYPE "specpriv-postprocess"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/LoopAA.h"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/FindUnderlyingObjects.h"
#include "scaf/Utilities/InstInsertPt.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/Utilities/SplitEdge.h"
#include "liberty/PointsToProfiler/Remat.h"
#include "liberty/Speculation/Api.h"

#include <list>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numJoined,           "Joined private ops");
STATISTIC(numEliminated,       "Eliminated private ops");
STATISTIC(numInvPromoted,      "Promoted invariant private ops");
STATISTIC(numDensePromoted,    "Densely promoted private ops");
STATISTIC(numSparsePromoted,   "Sparsely promoted private ops");
STATISTIC(numPrivSpecialized,  "Specialized private ops");
STATISTIC(numElimIntoCallee,   "Eliminations from eliminateIntoCallee");
STATISTIC(numElimOutOfCallee,  "Eliminations from eliminateOutOfCallee");
// STATISTIC(numDeadBlocksRemoved,"Dead blocks removed");
STATISTIC(numDeadEdgesRemoved, "Dead edges removed");

STATISTIC(numHeapCorrectUO,    "UO checks whose heap must be correct");
STATISTIC(numSubHeapCorrectUO, "UO checks whose sub-heap must be correct");
STATISTIC(numTrivialUO,        "Trivially satisfied UO checks removed");
STATISTIC(numHeapRedundantUO,  "Redundant UO heap checks removed");
STATISTIC(numSubHeapRedundantUO, "Redundant UO sub-heap checks removed");
STATISTIC(numInlinedUO,        "Inlined UO checks");

typedef std::set<Function *> FSet;
typedef std::multimap<BasicBlock*, Instruction*> CallsByBlock;
typedef std::vector<BasicBlock*> BBList;

// Find all calls to foi(), group them by function
static void groupByBlock(Constant *foi, FSet &fcns, CallsByBlock &group)
{
  for(Value::user_iterator i=foi->user_begin(), e=foi->user_end(); i!=e; ++i)
  {
    CallSite cs = getCallSite(*i);
    Instruction *old = cs.getInstruction();
    if( !old )
      continue;
    if( cs.getCalledFunction() != foi )
      continue;

    BasicBlock *bb = old->getParent();
    group.insert( CallsByBlock::value_type( bb, old ) );
    fcns.insert( bb->getParent() );
  }
}

// Find the first call to foi()
static Instruction *findFirstCallTo(Constant *foi)
{
  for(Value::user_iterator i=foi->user_begin(), e=foi->user_end(); i!=e; ++i)
  {
    CallSite cs = getCallSite(*i);
    Instruction *old = cs.getInstruction();
    if( !old )
      continue;
    if( cs.getCalledFunction() != foi )
      continue;

    return cs.getInstruction();
  }

  return 0;
}

/// Cut speculated control flow
struct Postprocess1 : public ModulePass
{
  static char ID;
  Postprocess1() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< LoopAA >();
    au.addRequired< ModuleLoops >();
  }

  bool runOnModule(Module &mod)
  {
    LLVM_DEBUG(errs() << "#################################################\n"
                 << " Post-Process 1\n\n\n");
    DEBUG_WITH_TYPE("specpriv-transform",
      errs() << "SpecPriv Postprocess-1: performing peephole optimizations.\n");

    api = Api(&mod);

    bool modified = false;

    // Cut control flow successors of misspeculation blocks.
    modified |= cutSpeculatedControlFlow();

    return modified;
  }

private:
  Api api;
  bool cutSpeculatedControlFlow()
  {
    bool modified = false;

    // For each call to the function __specpriv_misspec()
    Constant *misspec = api.getMisspeculate();
    std::vector<User*> users( misspec->user_begin(), misspec->user_end() );
    for(std::vector<User*>::iterator i=users.begin(), e=users.end(); i!=e; ++i)
    {
      Instruction *user = dyn_cast< Instruction >( *i );
      if( !user )
        continue;

      CallSite cs = getCallSite(user);
      if( !cs.getInstruction() )
        continue;
      if( cs.getCalledFunction() != misspec )
        continue;

      BasicBlock *bb = user->getParent();

      LLVMContext &ctx = bb->getParent()->getContext();

/*
      BBList killed;
      killOne(killed, bb);
*/
      Instruction *term = bb->getTerminator();
      for(unsigned j=0; j<term->getNumSuccessors(); ++j)
        term->getSuccessor(j)->removePredecessor(bb);
      term->eraseFromParent();
      new UnreachableInst(ctx, bb);
      modified = true;

/*
      // Transitively remove dead blocks.
      while( ! killed.empty() )
      {
        BasicBlock *dead = killed.back();
        killed.pop_back();

        killOne(killed, dead);

        dead->dropAllReferences();
        dead->eraseFromParent();
        ++numDeadBlocksRemoved;
      }
*/

      // Speculatively dead blocks are, by construction,
      // infrequent.  Move them out of the way, so that
      // the common case is densely packed into a smaller
      // footprint
      bb->moveAfter( & bb->getParent()->back() );
    }
    return modified;
  }

  void killOne(BBList &killed, BasicBlock *dead)
  {
    Instruction *term = dead->getTerminator();

    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      BasicBlock *succ = term->getSuccessor(sn);

      if( succ->getUniquePredecessor() == dead )
        killed.push_back(succ);

      succ->removePredecessor(dead);
      ++numDeadEdgesRemoved;
    }
  }


};

static int getHeapFromAllocator(Api &api, CallSite cs)
{
  if( ! cs.getInstruction() )
    return HeapAssignment::Unclassified;

  Function *allocator = cs.getCalledFunction();
  if( !allocator )
    return HeapAssignment::Unclassified;

  for(HeapAssignment::Type i = HeapAssignment::FirstHeap; i<=HeapAssignment::LastHeap; i=HeapAssignment::Type(i+1))
    if( allocator == api.getAlloc( i ) )
      return i;

  return HeapAssignment::Unclassified;
}
static int getSubHeapFromAllocator(CallSite &cs)
{
  return cast<ConstantInt>( cs.getArgument(1) )->getSExtValue();
}

static int getHeapFromUO(CallSite cs)
{
  return cast< ConstantInt >( cs.getArgument(1) )->getSExtValue();
}
static int getSubHeapFromUO(CallSite cs)
{
  return cast< ConstantInt >( cs.getArgument(2) )->getSExtValue();
}

// Eliminate, join or promote speculation checks
struct Postprocess2 : public ModulePass
{
  static char ID;
  Postprocess2() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< LoopAA >();
    au.addRequired< ModuleLoops >();
  }

  bool runOnModule(Module &mod)
  {
    LLVM_DEBUG(errs() << "#################################################\n"
                 << " Post-Process 2\n\n\n");
    DEBUG_WITH_TYPE("specpriv-transform",
      errs() << "SpecPriv Postprocess-2: performing peephole optimizations.\n");

    api = Api(&mod);

    bool modified = false;

    // Optimize UO checks.
    // - trivially satisfied UO checks
    // - redundant UO checks
    modified |= removeUnnecessaryUOChecks();

    // Join, Eliminate, Promote repeatedly
    // until convergence.
    for(;;)
    {
      bool iterationModified = false;

      for(;;)
      {
        iterationModified = false;

        iterationModified |= eliminate();
        iterationModified |= join();
        iterationModified |= promote(false);

        if( !iterationModified )
          break;
      }

      if( !iterationModified )
        iterationModified |= promote(true);

      if( !iterationModified )
        break;

      modified |= iterationModified;
    }

    return modified;
  }

private:
  Api api;
  Function *fcn;


  bool removeUnnecessaryUOChecks()
  {
    Constant *uo = api.getUO();

    bool modified = false;

    FSet fcns;
    CallsByBlock group;
    groupByBlock(uo, fcns, group);
    for(FSet::const_iterator i=fcns.begin(), e=fcns.end(); i!=e; ++i)
    {
      Function *fcn = *i;
      modified |= removeUnnecessaryUOChecksWithinFcn(fcn, group);
    }

    return modified;
  }

  bool removeRedundantUOChecksBetweenBlocks(CallsByBlock &group, BasicBlock *bbi, BasicBlock *bbj)
  {
    bool modified = false;

    IntegerType *u64 = Type::getInt64Ty(bbi->getContext());

    for(CallsByBlock::iterator k=group.lower_bound(bbi); k!=group.upper_bound(bbi); ++k)
    {
      CallSite csi = getCallSite( k->second );
      Value *obji = csi.getArgument(0)->stripPointerCasts();
      const int heapi = getHeapFromUO(csi);
      const int subheapi = getSubHeapFromUO(csi);
      if( -1 == heapi && -1 == subheapi )
        continue;

      for(CallsByBlock::iterator l=group.lower_bound(bbj); l!=group.upper_bound(bbj); ++l)
      {
        CallSite csj = getCallSite( l->second );
        Value *objj = csj.getArgument(0)->stripPointerCasts();

        if( obji != objj )
          continue;

        // One of these two is redundant.
        // Remove only the later one.

        // Within a block, it is unclear which is
        // later. TODO: be more precise
        if( bbi == bbj )
          continue;

        const int heapj = getHeapFromUO(csj);
        const int subheapj = getSubHeapFromUO(csj);

        // remove redundant heap check
        if( heapj != -1 )
          if( heapi == heapj )
          {
            csj.setArgument(1, ConstantInt::get(u64,-1) );
            modified = true;
            ++numHeapRedundantUO;
          }
        if( subheapj != -1 )
          if( subheapi == subheapj )
          {
            csj.setArgument(2, ConstantInt::get(u64,-1) );
            modified = true;
            ++numSubHeapRedundantUO;
          }
      }
    }

    return modified;
  }

  bool removeUnnecessaryUOChecksWithinFcn(Function *fcn, CallsByBlock &group)
  {
    bool modified = false;

    IntegerType *u64 = Type::getInt64Ty(fcn->getContext());

    const DataLayout &DL = fcn->getParent()->getDataLayout();

    // First, remove UO checks which will trivially succeed.
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
    {
      BasicBlock *bb = &*i;

      for(CallsByBlock::iterator j=group.lower_bound(bb); j!=group.upper_bound(bb); ++j)
      {
        CallSite cs = getCallSite( j->second );
        Instruction *check = cs.getInstruction();
        assert( check );

        Value *obj = cs.getArgument(0);
        int heap = getHeapFromUO(cs);
        int subheap = getSubHeapFromUO(cs);

        // We are checking that obj is in a particular heap.
        // Can we determine that all possible sources of this
        // object were allocations from that heap?

        UO sources;
        GetUnderlyingObjects(obj, sources, DL);

        bool heapMustBeCorrect = true;
        bool subheapMustBeCorrect = true;
        for(UO::iterator k=sources.begin(); k!=sources.end() && (heapMustBeCorrect || subheapMustBeCorrect); ++k)
        {
          const Value *src = *k;
          CallSite allocator = getCallSite(src);
          if( ! allocator.getInstruction() )
          {
            heapMustBeCorrect = false;
            subheapMustBeCorrect = false;
            break;
          }

          if (Function *func = allocator.getCalledFunction()) {
            std::string funcName = func->getName().str();
            if (funcName.rfind("__specpriv_alloc_", 0) != 0) {
              // not a specpriv_alloc call.
              // This could happen when a specpriv_alloc is called somewhere
              // inside another function and the returned value of this other
              // function is the pointer produced by the specpriv_alloc
              heapMustBeCorrect = false;
              subheapMustBeCorrect = false;
              break;
            }
          }

          if( getHeapFromAllocator(api, allocator) != heap )
            heapMustBeCorrect = false;

          if( getSubHeapFromAllocator(allocator) != subheap )
            subheapMustBeCorrect = false;
        }

        if( heapMustBeCorrect && -1 != heap )
        {
          LLVM_DEBUG(errs() << "Heap must be correct in " << *check << '\n');
          cs.setArgument(1, ConstantInt::get(u64,-1));
          ++numHeapCorrectUO;
        }
        if( subheapMustBeCorrect && -1 != subheap )
        {
          LLVM_DEBUG(errs() << "Sub-heap must be correct in " << *check << '\n');
          cs.setArgument(2, ConstantInt::get(u64,-1));
          ++numSubHeapCorrectUO;
        }

        /* move to the inlining phase
        // If we have removed both the heap and the subheap, then delete
        // the check
        if( uoCheckDoesNothing(cs) )
        {
          LLVM_DEBUG(errs() << "Removing a trivially satisfied check.\n  " << *check << '\n');
          check->eraseFromParent();
          CallsByBlock::iterator toErase = j;
          ++j;
          group.erase(toErase);
          ++numTrivialUO;
        }
        else
        {
          ++j;
        }
        */
      }
    }

    // Next, remove redundant UO checks
    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    DominatorTree &dt = mloops.getAnalysis_DominatorTree(fcn);

    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
    {
      BasicBlock *bbi = &*i;
      for(Function::iterator j=fcn->begin(); j!=e; ++j)
      {
        BasicBlock *bbj = &*j;

        if( !dt.dominates(bbi,bbj) )
          continue;

        modified |= removeRedundantUOChecksBetweenBlocks(group, bbi, bbj);
      }
    }

    return modified;
  }

  bool areBackToback(Value *obj1, uint64_t size, Value *obj2)
  {
    if( BitCastInst *cast1 = dyn_cast< BitCastInst >(obj1) )
      obj1 = cast1->getOperand(0);
    GetElementPtrInst *gep1 = dyn_cast< GetElementPtrInst >(obj1);
    if( !gep1 )
      return false;

    if( BitCastInst *cast2 = dyn_cast< BitCastInst >(obj2) )
      obj2 = cast2->getOperand(0);
    GetElementPtrInst *gep2 = dyn_cast< GetElementPtrInst >(obj2);
    if( !gep2 )
      return false;

    if( gep1->getPointerOperand() != gep2->getPointerOperand() )
      return false;

    gep_type_iterator tys = gep_type_begin(gep1);
    User::op_iterator idx1 = gep1->idx_begin(), idx2 = gep2->idx_begin();

    unsigned n;
    for(n=0 ; idx1 != gep1->idx_end(); ++tys, ++idx1, ++idx2, ++n)
    {
      if( *idx1 != *idx2 )
        break;
    }

    if( n != gep1->getNumIndices() - 1 )
      return false;

    ConstantInt *o1 = dyn_cast< ConstantInt >(*idx1);
    if( !o1 )
      return false;

    ConstantInt *o2 = dyn_cast< ConstantInt >(*idx2);
    if( !o2 )
      return false;

    const DataLayout &td = gep1->getParent()->getParent()->getParent()->getDataLayout();

    Type *ty = tys.getIndexedType();
    const uint64_t index1 = o1->getLimitedValue(), index2 = o2->getLimitedValue();
    uint64_t offset1=0, offset2=0;
    if( StructType *structty = dyn_cast< StructType >(ty) )
    {
      const StructLayout *layout = td.getStructLayout(structty);
      offset1 = layout->getElementOffset( index1 );
      offset2 = layout->getElementOffset( index2 );
    }
    else if( SequentialType *seqty = dyn_cast< SequentialType >(ty) )
    {
      const uint64_t alignment = td.getABITypeAlignment( seqty->getElementType() );

      offset1 = alignment * index1;
      offset2 = alignment * index2;
    }
    else
      return false;

    return (offset1 + size == offset2);
  }

  bool joinSameWithinBlock(BasicBlock *bb, CallsByBlock &writes)
  {
    bool modified = false;

    // Foreach pair of writes
    CallsByBlock::iterator low = writes.lower_bound(bb);
    for(CallsByBlock::iterator i=low; i!=writes.upper_bound(bb); ++i)
    {
      CallSite cs1 = getCallSite( i->second );
      ConstantInt *sz1 = dyn_cast< ConstantInt >( cs1.getArgument(1) );
      if( !sz1 )
        continue;
      const uint64_t size1 = sz1->getLimitedValue();

      Value *obj1 = cs1.getArgument(0);
      if( BitCastInst *cast1 = dyn_cast< BitCastInst >(obj1) )
        obj1 = cast1->getOperand(0);
      GetElementPtrInst *gep1 = dyn_cast< GetElementPtrInst >(obj1);
      if( !gep1 )
        continue;

      for(;;)
      {
        bool joinedOne = false;

        CallsByBlock::iterator j=i;
        for(++j; j!=writes.upper_bound(bb); ++j)
        {
          CallSite cs2 = getCallSite( j->second );

          // TODO: ensure there is no operation between these
          // which expects to see the intermediate value.

          ConstantInt *sz2 = dyn_cast< ConstantInt >( cs2.getArgument(1) );
          if( !sz2 )
            continue;
          const uint64_t size2 = sz2->getLimitedValue();

          Value *obj2 = cs2.getArgument(0);


          ConstantInt *sum = ConstantInt::get(sz1->getType(), size1+size2);

          if( areBackToback(obj1, size1, obj2) )
          {
            LLVM_DEBUG(errs() << '\n'
                   << "Joining adjacent ops:\n"
                   << " cs1: " << *cs1.getInstruction() << '\n'
                   << " cs2: " << *cs2.getInstruction() << '\n');

            // Update cs1; eliminate cs2.
            cs1.setArgument(1, sum);

            Instruction *dead = cs2.getInstruction();
            dead->eraseFromParent();

            LLVM_DEBUG(errs() << "  ==> " << *cs1.getInstruction() << '\n');

            modified = true;
            ++numJoined;

            // invalidated iterators
            joinedOne = true;
            writes.erase(j);
            break;
          }
          else if( areBackToback(obj2, size2, obj1) )
          {
            LLVM_DEBUG(errs() << '\n'
                   << "Joining adjacent ops:\n"
                   << " cs2: " << *cs2.getInstruction() << '\n'
                   << " cs1: " << *cs1.getInstruction() << '\n');

            // Update cs2; eliminate cs1
            cs2.setArgument(1, sum);

            Instruction *dead = cs1.getInstruction();
            dead->eraseFromParent();

            LLVM_DEBUG(errs() << "  ==> " << *cs2.getInstruction() << '\n');

            modified = true;
            ++numJoined;

            // Invalidated iterators on outer loop.
            writes.erase(i);
            joinedOne = true;

            // fuckit; let the outer loop bring us back
            return true;
          }
        }

        if( !joinedOne )
          break;
      }
    }

    return modified;
  }

  bool joinWithinBlock(BasicBlock *bb, CallsByBlock &writes, CallsByBlock &reads)
  {
    bool modified = false;

    if( writes.count(bb) && !reads.count(bb) )
      modified |= joinSameWithinBlock(bb, writes);
    else if( reads.count(bb) && !writes.count(bb) )
      modified |= joinSameWithinBlock(bb, reads);

    return modified;
  }


  bool joinWithinFcn(CallsByBlock &writes, CallsByBlock &reads)
  {
    bool modified = false;

    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
      modified |= joinWithinBlock(&*i, writes, reads);

    return modified;
  }

  bool join()
  {
    bool modified = false;

    Constant *write_range = api.getPrivateWriteRange();
    Constant *read_range = api.getPrivateReadRange();

    FSet fcns;
    CallsByBlock writes, reads;
    groupByBlock(write_range, fcns, writes);
    groupByBlock(read_range, fcns, reads);

    // Foreach function which calls write
    for(FSet::iterator i=fcns.begin(), e=fcns.end(); i!=e; ++i)
    {
      fcn = *i;

      // Operate on callsites within this function
      modified |= joinWithinFcn(writes, reads);
    }

    Constant *sharewrite_range = api.getSharePrivateWriteRange();

    FSet sharefcns;
    CallsByBlock sharewrites, sharereads;
    groupByBlock(sharewrite_range, sharefcns, sharewrites);

    // Foreach function which calls write
    for(FSet::iterator i=sharefcns.begin(), e=sharefcns.end(); i!=e; ++i)
    {
      fcn = *i;

      // Operate on callsites within this function
      modified |= joinWithinFcn(sharewrites, sharereads);
    }

    return modified;
  }

  unsigned offsetWithinBlock(BasicBlock *bb, Instruction *inst)
  {
    unsigned n=0;
    for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i, ++n)
      if( inst == &*i )
        return n;
    assert(false && "Not present");
  }

  bool eliminateBetweenBlocks(CallsByBlock &writes, CallsByBlock &reads, BasicBlock *A, BasicBlock *B, Loop *loop)
  {
    bool modified = false;

    LLVM_DEBUG(
      errs() << "Block " << A->getParent()->getName() << "::" << A->getName() << " vs block " << B->getParent()->getName() << "::" << B->getName();
      if( loop )
      {
        BasicBlock *header = loop->getHeader();
        errs() << " In loop " << header->getParent()->getName() << "::" << header->getName();
      }
      errs() << '\n';
    );

    if( writes.count(A) == 0 )
    {
      LLVM_DEBUG(errs() << "  No op1s in block A\n");
      return false;
    }
    else if( reads.count(B) == 0 )
    {
      LLVM_DEBUG(errs() << "  No op2s in block B\n");
      return false;
    }

    LoopAA *aa = getAnalysis< LoopAA >().getTopAA();
    Remedies R;

    // For each write from A
    for(CallsByBlock::iterator k=writes.lower_bound(A); k!=writes.upper_bound(A); ++k)
    {
      Instruction *write = k->second;

      LLVM_DEBUG(errs() << "   first: " << *write << '\n');

      CallSite cs(write);
      Value *wobj = cs.getArgument(0);

      while( CastInst *cast = dyn_cast< CastInst >(wobj) )
        wobj = cast->getOperand(0);

      ConstantInt *sz = dyn_cast< ConstantInt >( cs.getArgument(1) );
      if( !sz )
        continue;
      const uint64_t size1 = sz->getLimitedValue();

      bool deletedOne = true;
      while( deletedOne )
      {
        deletedOne = false;

        for(CallsByBlock::iterator l=reads.lower_bound(B), z=reads.upper_bound(B); l!=z; ++l)
        {
          Instruction *read = l->second;

          // Either A strictly-dominates B, or write is before read within the same block.
          if( A == B )
            if( offsetWithinBlock(A,write) >= offsetWithinBlock(A,read) )
              continue;

          LLVM_DEBUG(errs() << "  second: " << *read << '\n');

          CallSite cs(read);
          Value *robj = cs.getArgument(0);

          while( CastInst *cast = dyn_cast< CastInst >(robj) )
            robj = cast->getOperand(0);

          ConstantInt *sz = dyn_cast< ConstantInt >( cs.getArgument(1) );
          if( !sz )
            continue;
          const uint64_t size2 = sz->getLimitedValue();

          if( size1 < size2 )
            continue;

          // Overlap?
          LoopAA::AliasResult res =
              aa->alias(wobj, size1 - size2 + 1, LoopAA::Same, robj, 1, loop, R,
                        LoopAA::DMustAlias);
          LLVM_DEBUG(errs() << "    result => " << res << '\n');
          if( res == LoopAA::MustAlias )
          {
            LLVM_DEBUG(errs() << '\n'
                   << "Eliminating dominated op:\n"
                   << "     write: " << *write << '\n'
                   << " dominates: " << *read << '\n');

            read->eraseFromParent();
            ++numEliminated;
            modified = true;

            // we invalidated our iterators :(
            reads.erase(l);
            deletedOne = true;
            break;
          }
        }
      }
    }

    return modified;
  }

  // Given a pair of blocks A, B s.t. A dom B,
  // eliminate primitives within functions called from A.
  bool eliminateOutOfCallee(CallsByBlock &writes, CallsByBlock &reads, BasicBlock *A, BasicBlock *B, Loop *loop)
  {
    bool modified = false;

    // If block A contains function calls, then
    // the blocks which postdominate the entry to the
    // callsite dominates everything in B.
    // If that callee has a unique callsite within
    // the RoI, then those blocks from the callee
    // dominate everything in B.
    for(BasicBlock::iterator i=B->begin(), e=B->end(); i!=e; ++i)
    {
      CallSite cs = getCallSite( &*i );
      if( !cs.getInstruction() )
        continue;

      Function *callee = cs.getCalledFunction();
      if( !callee )
        continue;

      if( callee->isDeclaration() )
        continue;

      if( !callee->hasOneUse() )
        continue;

      // Cool, eliminate out of the callee
      ModuleLoops &mloops = getAnalysis< ModuleLoops >();
      PostDominatorTree &pdt = mloops.getAnalysis_PostDominatorTree(callee);

      // For each block cc s.t. cc postdom entry
      for(Function::iterator j=callee->begin(), z=callee->end(); j!=z; ++j)
      {
        BasicBlock *cc = &*j;
        if( ! pdt.dominates(cc, &callee->getEntryBlock() ) )
          continue;

        if( eliminateB2B(writes, reads, cc,B, loop) )
        {
          ++numElimOutOfCallee;
          modified = true;
        }
      }
    }

    return modified;
  }

  // Given a pair of blocks A, B s.t. A dom B,
  // eliminate primitives within functions called from B.
  bool eliminateIntoCallee(CallsByBlock &writes, CallsByBlock &reads, BasicBlock *A, BasicBlock *B, Loop *loop)
  {
    bool modified = false;

    // If block B contains function calls, then
    // A dominates everything within that call site.
    // If that callee has a unique callsite within
    // the RoI, then A dominates every block within
    // the callee function.
    for(BasicBlock::iterator i=B->begin(), e=B->end(); i!=e; ++i)
    {
      CallSite cs = getCallSite( &*i );
      if( !cs.getInstruction() )
        continue;

      Function *callee = cs.getCalledFunction();
      if( !callee )
        continue;

      if( callee->isDeclaration() )
        continue;

      if( !callee->hasOneUse() )
        continue;

      // Cool, eliminate into callee.
      for(Function::iterator j=callee->begin(), z=callee->end(); j!=z; ++j)
        if( eliminateB2B(writes,reads, A, &*j, loop) )
        {
//          errs() << "Eliminated from " << A->getParent()->getName() << " :: " << A->getName() << " to " << callee->getName() << " :: " << j->getName() << '\n';
          ++numElimIntoCallee;
          modified = true;
        }
    }

    return modified;
  }

  // Given two blocks A, B s.t. block A always runs before block B during EVERY iteration,
  // eliminate redundant primitives from B.
  bool eliminateB2B(CallsByBlock &writes, CallsByBlock &reads, BasicBlock *A, BasicBlock *B, Loop *loop)
  {
    bool modified = false;

    // Writes make reads succeed
    LLVM_DEBUG(errs() << "write-vs-read\n");
    modified |= eliminateBetweenBlocks(writes, reads,  A,B, loop);

    // Writes are idempotent
    LLVM_DEBUG(errs() << "write-vs-write\n");
    modified |= eliminateBetweenBlocks(writes, writes, A,B, loop);

    // Successful reads are idempotent
    // (the second can only fail if the first failed too)
    LLVM_DEBUG(errs() << "read-vs-read\n");
    modified |= eliminateBetweenBlocks(reads,  reads,  A,B, loop);

/* -- No longer true: if the read occurs before any write.
    // Successful read implies and earlier write, which is idempotent with later writes
    LLVM_DEBUG(errs() << "read-vs-write\n");
    modified |= eliminateBetweenBlocks(reads, writes,  A,B, loop);
*/

    return modified;
  }

  bool eliminateWithinFcn(CallsByBlock &writes, CallsByBlock &reads)
  {
    bool modified = false;

    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    DominatorTree &dt = mloops.getAnalysis_DominatorTree(fcn);
    mloops.getAnalysis_PostDominatorTree(fcn);
    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);

    // TODO: ensure that blocks A and B both belong to the
    // same parallel invocation!

    // Foreach pair A, B of basic blocks s.t. A dom B:
    for(Function::iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
    {
      BasicBlock *A = &*i;

      for(Function::iterator j=fcn->begin(); j!=e; ++j)
      {
        BasicBlock *B = &*j;

        if( !dt.dominates(A,B) )
          continue;
        LLVM_DEBUG(
          errs() << "Block "
          << A->getParent()->getName() << "::" << A->getName()
          << " dominates  block "
          << B->getParent()->getName() << "::" << B->getName() << '\n');


        // Find the innermost loop which contains both
        Loop *loop = 0;
        for(loop = li.getLoopFor(A); loop && !loop->contains(B); loop=loop->getParentLoop() )
          {}

        // Either (loop==0), or loop contains both A and B.

        // Try to eliminate within this, and all parent loops.
        for(;;)
        {
          modified |= eliminateB2B(writes,reads, A, B, loop);

          // If block B contains function calls, then
          // A dominates everything within that call site.
          // If that callee has a unique callsite within
          // the RoI, then A dominates every block within
          // the callee.
          modified |= eliminateIntoCallee(writes,reads, A,B, loop);

          // If block A contains function calls, then
          // the blocks which postdominate the entry to the
          // callsite dominates everything in B.
          // If that callee has a unique callsite within
          // the RoI, then those blocks from the callee
          // dominate everything in B.
          modified |= eliminateOutOfCallee(writes,reads, A,B, loop);

          // Also check loops which contain this.
          if( !loop )
            break;
          loop = loop->getParentLoop();
        }
      }
    }

    return modified;
  }

  bool eliminate()
  {
    bool modified = false;

    Constant *write_range = api.getPrivateWriteRange();
    Constant *read_range = api.getPrivateReadRange();

    // Collect all uses of each, grouped by function.
    FSet fcns;
    CallsByBlock writes, reads;
    groupByBlock(write_range, fcns, writes);
    groupByBlock(read_range, fcns, reads);

    // Foreach function which calls write
    for(FSet::iterator i=fcns.begin(), e=fcns.end(); i!=e; ++i)
    {
      fcn = *i;

      // Operate on callsites within this function
      modified |= eliminateWithinFcn(writes, reads);
    }

    Constant *sharewrite_range = api.getSharePrivateWriteRange();
    CallsByBlock sharewrites, sharereads;
    FSet sharefcns;
    groupByBlock(sharewrite_range, sharefcns, sharewrites);
    // Foreach function which calls write
    for(FSet::iterator i=sharefcns.begin(), e=sharefcns.end(); i!=e; ++i)
    {
      fcn = *i;

      // Operate on callsites within this function
      modified |= eliminateWithinFcn(sharewrites, sharereads);
    }

    return modified;
  }

  bool promoteOpWithinLoop(ScalarEvolution &scev, Loop *loop, CallsByBlock &writes, Instruction *op, bool isWrite, bool AllowSparse)
  {
    // Safety test:
    // Only promote it if we are SURE that it executes with EVERY iteration.
    // At the moment, this means in the header.  A richer test is possible. TODO
    if( op->getParent() != loop->getHeader() )
      return false;

    CallSite cs = getCallSite( op );

    const DataLayout &td = op->getParent()->getParent()->getParent()->getDataLayout();
    Remat remat;

    LLVMContext &ctx = op->getParent()->getParent()->getContext();
    Type *voidptr = PointerType::getUnqual( Type::getInt8Ty(ctx) );
    Type *u32 = Type::getInt32Ty(ctx);
    Constant *one = ConstantInt::get(u32,1);

    // Size of the operation
    ConstantInt *sz = dyn_cast< ConstantInt >( cs.getArgument(1) );
    if( !sz )
      return false;
    const uint64_t size = sz->getLimitedValue();

    Value *ptr = cs.getArgument(0);
    const SCEV *s = scev.getSCEV(ptr);
    if( scev.isLoopInvariant(s,loop) )
    {
      Value *obj = ptr;
      BitCastInst *cast = dyn_cast< BitCastInst >(obj);
      if( cast )
        obj = cast;

      // Ensure that the value is available before the loop
      if( Instruction *iobj = dyn_cast< Instruction >(obj) )
        if( loop->contains( iobj->getParent() ) )
          return false;

/*
      if( isWrite )
      {
        // Move the bitcast and call to the loop exit
        SmallVector<BasicBlock*,1> exitings;
        loop->getExitingBlocks(exitings);
        for(SmallVector<BasicBlock*,1>::iterator i=exitings.begin(), e=exitings.end(); i!=e; ++i)
        {
          BasicBlock *src = *i;
          Instruction *term = src->getTerminator();
          for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
          {
            BasicBlock *dst = term->getSuccessor(sn);
            if( loop->contains(dst) )
              continue;

            BasicBlock *exit = split(src,dst);
            InstInsertPt where = InstInsertPt::End(exit);

            Value *arg = obj;
            if( obj->getType() != voidptr )
            {
              Instruction *cast = new BitCastInst(obj,voidptr);
              where << cast;
              arg = cast;
            }

            Instruction *clone = op->clone();
            clone->setOperand(0, arg);
            where << clone;
          }
        }

        ++numInvPromoted;
        op->eraseFromParent();

        // Invalidated our loop!
        ModuleLoops &mloops = getAnalysis< ModuleLoops >();
        mloops.forget( loop->getHeader()->getParent() );
        return true;
      }
      else
*/
      {
        // Move the bitcast and call to the loop preheader
        BasicBlock *header = loop->getHeader();
        std::vector<BasicBlock*> preds( pred_begin(header), pred_end(header) );
        for(std::vector<BasicBlock*>::iterator i=preds.begin(), e=preds.end(); i!=e; ++i)
        {
          BasicBlock *bb = *i;
          if( loop->contains(bb) )
            continue;

          BasicBlock *preheader = split(bb,header);
          InstInsertPt where = InstInsertPt::End(preheader);

          Value *arg = obj;
          if( obj->getType() != voidptr )
          {
            Instruction *cast = new BitCastInst(obj,voidptr);
            where << cast;
            arg = cast;
          }

          Instruction *clone = op->clone();
          clone->setOperand(0, arg);
          where << clone;
        }

        ++numInvPromoted;
        op->eraseFromParent();

        // Invalidated our loop!
        ModuleLoops &mloops = getAnalysis< ModuleLoops >();
        mloops.forget( header->getParent() );
        return true;
      }
    }

    else if( scev.hasComputableLoopEvolution(s,loop) )
    {
      // Good.. in theory the loop pattern could be recomputed.

      const SCEVAddRecExpr *evo = dyn_cast< SCEVAddRecExpr >(s);
      if( !evo )
        return false;

      // Can we determine a stride?
      if( !evo->isAffine() )
        return false;

      const SCEV *stride = evo->getStepRecurrence(scev);

      if( !remat.canEvaluateToInteger(stride) )
        return false;
      const uint64_t istride = remat.evaluateToIntegerUnsafe(stride,td);
      Constant *cstride = ConstantInt::get(u32,istride);

      const SCEV *firstIteration = scev.getConstant(u32,0);
      const SCEV *firstPointer = evo->evaluateAtIteration( firstIteration, scev );

      if( istride == size )
      {
        // The operations are back-to-back... can be turned into one big operation.
/*
        if( isWrite )
        {
          Value *civ = loop->getCanonicalInductionVariable();
          assert( civ && "No CIV?!");

          LLVM_DEBUG(
          errs() << '\n'
                 << "  ...Performing DENSE Private-Store Promotion...\n"
                 << "         Loop: " << fcn->getName() << " :: " << loop->getHeader()->getName() << '\n'
                 << "           Op: " << *op << '\n'
                 << "First pointer: " << *firstPointer << '\n'
                 << "       Stride: " << istride << '\n'
                 << "         Size: " << size << '\n'
                 << "          Num: 1+" << *civ << '\n'
          );

          Constant *big_write = api.getPrivateWriteRange();

          SmallVector<BasicBlock*,1> exitings;
          loop->getExitingBlocks(exitings);
          for(SmallVector<BasicBlock*,1>::iterator i=exitings.begin(), e=exitings.end(); i!=e; ++i)
          {
            BasicBlock *src = *i;
            Instruction *term = src->getTerminator();
            for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
            {
              BasicBlock *dst = term->getSuccessor(sn);
              if( loop->contains(dst) )
                continue;

              BasicBlock *exit = split(src,dst);
//                if( loop->getParentLoop() )
//                  loop->getParentLoop()->addBasicBlockToLoop(preheader, li);

              InstInsertPt where = InstInsertPt::End(exit);
              Value *start = remat.remat(where, scev, firstPointer, td, voidptr);

              Value *num = civ;
              if( num->getType() != u32 )
              {
                Instruction *cast = new TruncInst(num,u32);
                where << cast;
                num = cast;
              }

              Instruction *add = BinaryOperator::CreateNSWAdd(num,one);
              Instruction *mul = BinaryOperator::CreateNSWMul(add, cstride);
              where << add << mul;

              Value *actuals[] = { start, mul };
              where << CallInst::Create(big_write, ArrayRef<Value*>(&actuals[0], &actuals[2]) );
            }
          }

          ++numDensePromoted;
          op->eraseFromParent();

          // Invalidated our loop!
          ModuleLoops &mloops = getAnalysis< ModuleLoops >();
          mloops.forget(fcn);
          return true;
        }
        else
*/
        {
          // Since loads will be moved BEFORE the loop, we must have a
          // loop invariant backedge-taken count.
          if( scev.hasLoopInvariantBackedgeTakenCount(loop) )
          {
            const SCEV *lastIteration  = scev.getBackedgeTakenCount(loop);

            LLVM_DEBUG(
            errs() << '\n'
                   << "  ...Performing DENSE Private-Load Promotion...\n"
                   << "         Loop: " << fcn->getName() << " :: " << loop->getHeader()->getName() << '\n'
                   << "           Op: " << *op << '\n'
                   << "First pointer: " << *firstPointer << '\n'
                   << "       Stride: " << istride << '\n'
                   << "         Size: " << size << '\n'
                   << "          Num: 1+" << *lastIteration << '\n'
            );



            BasicBlock *header = loop->getHeader();
            std::vector<BasicBlock*> preds( pred_begin(header), pred_end(header) );
            for(std::vector<BasicBlock*>::iterator i=preds.begin(), e=preds.end(); i!=e; ++i)
            {
              BasicBlock *bb = *i;
              if( loop->contains(bb) )
                continue;

              BasicBlock *preheader = split(bb,header);
  //                if( loop->getParentLoop() )
  //                  loop->getParentLoop()->addBasicBlockToLoop(preheader, li);

              InstInsertPt where = InstInsertPt::End(preheader);
              Value *start = remat.remat(where, scev, firstPointer, td, voidptr);
              Value *num = remat.remat(where, scev, lastIteration, td, u32);

              Instruction *add = BinaryOperator::CreateNSWAdd(num, one);
              Instruction *mul = BinaryOperator::CreateNSWMul(add, cstride);
              where << add << mul;

              if( isWrite )
              {
                Value *actuals[] = { start, mul };
                Constant *big_write = api.getPrivateWriteRange();
                where << CallInst::Create(big_write, ArrayRef<Value*>(&actuals[0], &actuals[2]) );
              }
              else
              {
                Value *actuals[] = { start, mul, cs.getArgument(2) };
                Constant *big_read = api.getPrivateReadRange();
                where << CallInst::Create(big_read, ArrayRef<Value*>(&actuals[0], &actuals[3]) );
              }
            }

            ++numDensePromoted;
            op->eraseFromParent();

            // Invalidated our loop!
            ModuleLoops &mloops = getAnalysis< ModuleLoops >();
            mloops.forget(fcn);
            return true;

          }
        }
      }

      // These two cases are less than ideal.  We will settle for them
      // after we try everything else first.
      if( AllowSparse && istride > size )
      {
/*
        // The operations are not back-to-back.
        // Still, we may achieve a performance benefit by extracting
        // this operations out of the loop, because we half the
        // cache footprint.
        if( isWrite )
        {
          Value *civ = loop->getCanonicalInductionVariable();
          assert( civ && "No CIV?!");

          LLVM_DEBUG(
          errs() << '\n'
                 << "  ...Performing Sparse Private-Store Promotion...\n"
                 << "         Loop: " << fcn->getName() << " :: " << loop->getHeader()->getName() << '\n'
                 << "           Op: " << *op << '\n'
                 << "First pointer: " << *firstPointer << '\n'
                 << "       Stride: " << istride << '\n'
                 << "         Size: " << size << '\n'
                 << "          Num: 1+" << *civ << '\n'
          );


          Constant *big_write = api.getPrivateWriteRangeStride();

          SmallVector<BasicBlock*,1> exitings;
          loop->getExitingBlocks(exitings);
          for(SmallVector<BasicBlock*,1>::iterator i=exitings.begin(), e=exitings.end(); i!=e; ++i)
          {
            BasicBlock *src = *i;
            Instruction *term = src->getTerminator();
            for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
            {
              BasicBlock *dst = term->getSuccessor(sn);
              if( loop->contains(dst) )
                continue;

              BasicBlock *exit = split(src,dst);
//                if( loop->getParentLoop() )
//                  loop->getParentLoop()->addBasicBlockToLoop(preheader, li);

              InstInsertPt where = InstInsertPt::End(exit);
              Value *start = remat.remat(where, scev, firstPointer, td, voidptr);

              Value *num = civ;
              if( num->getType() != u32 )
              {
                Instruction *cast = new TruncInst(num,u32);
                where << cast;
                num = cast;
              }

              Instruction *add = BinaryOperator::CreateNSWAdd(num,one);
              where << add;

              Value *actuals[] = { start, add, cstride, cs.getArgument(1) };
              where << CallInst::Create(big_write, ArrayRef<Value*>(&actuals[0], &actuals[4]) );
            }
          }

          ++numSparsePromoted;
          op->eraseFromParent();

          // Invalidated our loop!
          ModuleLoops &mloops = getAnalysis< ModuleLoops >();
          mloops.forget(fcn);
          return true;
        }
        else
*/
        {
          // Since loads will be moved BEFORE the loop, we must have a
          // loop invariant backedge-taken count.
          if( scev.hasLoopInvariantBackedgeTakenCount(loop) )
          {
            const SCEV *lastIteration  = scev.getBackedgeTakenCount(loop);

            LLVM_DEBUG(
            errs() << '\n'
                   << "  ...Performing Sparse Private-Load Promotion...\n"
                   << "         Loop: " << fcn->getName() << " :: " << loop->getHeader()->getName() << '\n'
                   << "           Op: " << *op << '\n'
                   << "First pointer: " << *firstPointer << '\n'
                   << "       Stride: " << istride << '\n'
                   << "         Size: " << size << '\n'
                   << "          Num: 1+" << *lastIteration << '\n'
            );



            BasicBlock *header = loop->getHeader();
            std::vector<BasicBlock*> preds( pred_begin(header), pred_end(header) );
            for(std::vector<BasicBlock*>::iterator i=preds.begin(), e=preds.end(); i!=e; ++i)
            {
              BasicBlock *bb = *i;
              if( loop->contains(bb) )
                continue;

              BasicBlock *preheader = split(bb,header);
  //                if( loop->getParentLoop() )
  //                  loop->getParentLoop()->addBasicBlockToLoop(preheader, li);

              InstInsertPt where = InstInsertPt::End(preheader);
              Value *start = remat.remat(where, scev, firstPointer, td, voidptr);
              Value *num = remat.remat(where, scev, lastIteration, td, u32);

              Instruction *add = BinaryOperator::CreateNSWAdd(num, one);
              where << add;

              if( isWrite )
              {
                Value *actuals[] = { start, add, cstride, cs.getArgument(1) };
                Constant *big_write = api.getPrivateWriteRangeStride();
                where << CallInst::Create(big_write, ArrayRef<Value*>(&actuals[0], &actuals[4]) );
              }
              else
              {
                Value *actuals[] = { start, add, cstride, cs.getArgument(1), cs.getArgument(2) };
                Constant *big_read = api.getPrivateReadRangeStride();
                where << CallInst::Create(big_read, ArrayRef<Value*>(&actuals[0], &actuals[5]) );
              }
            }

            ++numSparsePromoted;
            op->eraseFromParent();

            // Invalidated our loop!
            ModuleLoops &mloops = getAnalysis< ModuleLoops >();
            mloops.forget(fcn);
            return true;
          }
        }
      }
    }
    return false;
  }

  bool promoteNonInterfering(CallsByBlock &GroupA, CallsByBlock &GroupB, Loop *loop, ScalarEvolution &scev, bool isWrite, bool AllowSparse)
  {
    LoopAA *aa = getAnalysis< LoopAA >().getTopAA();
    Remedies R;

    // For each write operation...
    for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    {
      BasicBlock *bbi = *i;

      for(CallsByBlock::iterator wi=GroupA.lower_bound(bbi), we=GroupA.upper_bound(bbi); wi!=we; ++wi)
      {
        Instruction *ElementA = wi->second;

        // Is it disjoint from EVERY read operation?
        bool disjointFromAll = true;
        for(Loop::block_iterator j=loop->block_begin(), z=loop->block_end(); j!=z && disjointFromAll ; ++j)
        {
          BasicBlock *bbj = *j;

          for(CallsByBlock::iterator ri=GroupB.lower_bound(bbj), re=GroupB.upper_bound(bbj); ri!=re; ++ri)
          {
            Instruction *ElementB = ri->second;

            CallSite csw = getCallSite(ElementA);
            CallSite csr = getCallSite(ElementB);

            Value *wptr = csw.getArgument(0);
            while( CastInst *cast = dyn_cast< CastInst >(wptr) )
              wptr = cast->getOperand(0);

            Value *rptr = csr.getArgument(0);
            while( CastInst *cast = dyn_cast< CastInst >(rptr) )
              rptr = cast->getOperand(0);

            ConstantInt *wsz = dyn_cast< ConstantInt >( csw.getArgument(1) );
            ConstantInt *rsz = dyn_cast< ConstantInt >( csr.getArgument(1) );

            if( !rsz || !wsz )
            {
              disjointFromAll = false;
              break;
            }
            const uint64_t rsize = rsz->getLimitedValue();
            const uint64_t wsize = wsz->getLimitedValue();

            // Are they disjoint in the same iteration of this loop?
            if( aa->alias(wptr,wsize, LoopAA::Same, rptr,rsize, loop, R) != LoopAA::NoAlias )
            {
              disjointFromAll = false;
              break;
            }
            // Are they disjoint when the write occurs in an earlier iteration than the read?
            if( aa->alias(wptr,wsize, LoopAA::Before, rptr,rsize, loop, R) != LoopAA::NoAlias )
            {
              disjointFromAll = false;
              break;
            }
            // Are they disjoint when the write offurs in a later iteration than the read?
            if( aa->alias(wptr,wsize, LoopAA::After, rptr,rsize, loop, R) != LoopAA::NoAlias )
            {
              disjointFromAll = false;
              break;
            }
          }
        }

        if( !disjointFromAll )
          continue;

        // This write is disjoint from all reads.
        if( promoteOpWithinLoop(scev, loop, GroupA, ElementA, isWrite, AllowSparse) )
          return true; // invalidated loop info, must restart.
      }
    }

    return false;
  }

  bool promoteWithinLoop(ScalarEvolution &scev, LoopInfo &li, Loop *loop, CallsByBlock &writes, CallsByBlock &reads, bool AllowSparse)
  {
    unsigned numOther=0;
    for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    {
      BasicBlock *bb = *i;

      for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      {
        CallSite cs = getCallSite( &*j );
        Instruction *inst = cs.getInstruction();
        if( !inst )
          continue;

        if( isa< DbgInfoIntrinsic >(inst) )
          continue;

        Function *callee = cs.getCalledFunction();
        if( callee )
          if( callee->getName().find("__specpriv_") == 0 )
            continue;

        ++numOther;
      }
    }

    if( numOther > 0 )
      return false; // abandon this loop.

    if( promoteNonInterfering(writes, reads, loop, scev, true, AllowSparse) )
      return true; // invalidated loop info; restart.

    if( promoteNonInterfering(reads, writes, loop, scev, false, AllowSparse) )
      return true; // invalidated loop info; restart.

    return false;
  }

  bool promoteWithinFcn(CallsByBlock &writes, CallsByBlock &reads, bool AllowSparse)
  {
    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    ScalarEvolution &scev = mloops.getAnalysis_ScalarEvolution(fcn);
    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);


    // Flatten the tree of loops into a flat vector of all loops in this function
    typedef std::vector<Loop*> LoopList;
    LoopList flat( li.begin(), li.end() );
    for(unsigned i=0; i<flat.size(); ++i)
    {
      Loop *l = flat[i];
      flat.insert( flat.end(),
        l->begin(), l->end() );
    }

    // Visit loops in post-order (innermost loops first)
    for(int i=flat.size()-1; i>=0; --i)
      if( promoteWithinLoop(scev, li, flat[i], writes, reads, AllowSparse) )
        return true; // invalidated loop info; restart

    return false;
  }


  bool promote(bool AllowSparse)
  {
    Constant *write_range = api.getPrivateWriteRange();
    Constant *read_range = api.getPrivateReadRange();

    // Collect all uses of each, grouped by function.
    FSet fcns;
    CallsByBlock writes, reads;
    groupByBlock(write_range, fcns, writes);
    groupByBlock(read_range, fcns, reads);

    // Foreach function which calls write
    for(FSet::iterator i=fcns.begin(), e=fcns.end(); i!=e; ++i)
    {
      fcn = *i;

      // Operate on callsites within this function
      if( promoteWithinFcn(writes, reads, AllowSparse) )
        return true; // invalidated loop info; retstart
    }

    Constant *sharewrite_range = api.getSharePrivateWriteRange();

    // Collect all uses of each, grouped by function.
    FSet sharefcns;
    CallsByBlock sharewrites, sharereads;
    groupByBlock(sharewrite_range, sharefcns, sharewrites);

    // Foreach function which calls write
    for(FSet::iterator i=sharefcns.begin(), e=sharefcns.end(); i!=e; ++i)
    {
      fcn = *i;

      // Operate on callsites within this function
      if( promoteWithinFcn(sharewrites, sharereads, AllowSparse) )
        return true; // invalidated loop info; retstart
    }

    return false;
  }



};

// Specialize speculation checks
struct Postprocess3 : public ModulePass
{
  static char ID;
  Postprocess3() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< LoopAA >();
    au.addRequired< ModuleLoops >();
  }

  bool runOnModule(Module &mod)
  {
    LLVM_DEBUG(errs() << "#################################################\n"
                 << " Post-Process 3\n\n\n");

    DEBUG_WITH_TYPE("specpriv-transform",
      errs() << "SpecPriv Postprocess-3: performing peephole optimizations.\n");

    api = Api(&mod);

    bool modified = false;

    // Specialize calls to __specpriv_write_range() and __specpriv_read_range()
    // according to the size of the operation.
    modified |= specializePrivateOps();

    // Inline UO checks
    modified |= inlineUOChecks();

    return modified;
  }

private:
  Api api;

  bool inlineUOChecks()
  {
    Constant *uo = api.getUO();

    bool modified = false;

    for(;;)
    {
      Instruction *call = findFirstCallTo(uo);
      if( !call )
        break;

      modified |= inlineUOCheck(call);
    }

    return modified;
  }

  bool inlineUOCheck(Instruction *check)
  {
    BasicBlock *checkbb = check->getParent();
    Function *fcn = checkbb->getParent();
    LLVMContext &ctx = fcn->getContext();
    IntegerType *u64 = Type::getInt64Ty(ctx);

    CallSite cs = getCallSite( check );
    assert( check );

    Value *obj = cs.getArgument(0);
    int heap = getHeapFromUO(cs);
    int subheap = getSubHeapFromUO(cs);

    Value *message = cs.getArgument(3);

    // Before: __specpriv_uo(ptr,heap,subheap,message)
    // After:  tmp1 = inttoptr ptr
    //         heap.masked = and tmp1, MASK
    //         heap.ok = icmp heap.masked,heapcodes[heap]
    //         subheap.masked = and tmp1, MASK
    //         subheap.ok = icmp subheap.masked,subheap
    //         spec.ok = and heap.ok, subheap.ok
    //         br spec.ok, continue, uo.fail.
    //
    //    ...
    //    uo.fail:
    //         __specpriv_misspec(message)
    //         unreachable

    // Must check one or both of {heap, sub-heap}
    if( -1 == heap && -1 == subheap )
      ++numTrivialUO;

    else
    {
      ++numInlinedUO;

      BasicBlock *goodbb = checkbb->splitBasicBlock(check, "_speculation_ok");
      // Remove the terminator created by splitBasicBlock
      checkbb->getTerminator()->eraseFromParent();
      InstInsertPt where = InstInsertPt::End(checkbb);

      BasicBlock *misspecbb = BasicBlock::Create(ctx, "uo.fail.", fcn);

      Instruction *cast = new PtrToIntInst(obj, u64, "ptr2int");
      where << cast;

      Value *heap_ok = ConstantInt::getTrue(ctx);
      if( heap != -1 )
      {
        ConstantInt *mask = ConstantInt::get(u64, Api::getHeapCodeMask());
        Instruction *masked = BinaryOperator::Create(
          BinaryOperator::And, cast, mask);
        ConstantInt *pattern = ConstantInt::get(u64, Api::getCodeForHeap( (HeapAssignment::Type)heap ));
        Instruction *spec_ok = CmpInst::Create(
          Instruction::ICmp, ICmpInst::ICMP_EQ, masked, pattern, "heap.ok");
        where << masked
              << spec_ok;
        heap_ok = spec_ok;
      }

      Value *subheap_ok = ConstantInt::getTrue(ctx);
      if( subheap != -1 )
      {
        // no subheaps currently used
        /*
        ConstantInt *mask = ConstantInt::get(u64, Api::getSubHeapCodeMask());
        Instruction *masked = BinaryOperator::Create(
          BinaryOperator::And, cast, mask);
        ConstantInt *pattern = ConstantInt::get(u64, Api::getCodeForSubHeap(subheap));
        Instruction *spec_ok = CmpInst::Create(
          Instruction::ICmp, ICmpInst::ICMP_EQ, masked, pattern, "subheap.ok" );
        where << masked
              << spec_ok;
        subheap_ok = spec_ok;
        */
      }

      // check for null ptr
      ConstantInt *null_ptr = ConstantInt::get(u64, 0);
      Instruction *null_ptr_check = CmpInst::Create(
          Instruction::ICmp, ICmpInst::ICMP_EQ, cast, null_ptr, "null.ptr.check");
      where << null_ptr_check;

      BinaryOperator *all_spec_ok=
        BinaryOperator::Create(Instruction::And, heap_ok, subheap_ok, "all.spec.ok");

      BinaryOperator *conjunction=
        BinaryOperator::Create(Instruction::Or, null_ptr_check, all_spec_ok, "spec.ok");
      BranchInst *br = BranchInst::Create(goodbb, misspecbb, conjunction);

      where << all_spec_ok << conjunction << br;

      // fill in the misspec bb
      where = InstInsertPt::Beginning(misspecbb);
      Value *actuals[] = { message };
      where << CallInst::Create( api.getMisspeculate(), ArrayRef<Value*>(&actuals[0], &actuals[1]) )
            << new UnreachableInst(ctx);
    }

    // Remove the old call
    check->eraseFromParent();

    return true;
  }

  bool specializePrivateOps()
  {
    bool modified = false;

    Constant *write_range = api.getPrivateWriteRange();
    Constant *read_range = api.getPrivateReadRange();

    std::vector<User*> users( write_range->user_begin(), write_range->user_end() );
    for(std::vector<User*>::iterator i=users.begin(), e=users.end(); i!=e; ++i)
    {
      CallSite cs = getCallSite(*i);
      Instruction *old = cs.getInstruction();
      if( !old )
        continue;
      if( cs.getCalledFunction() != write_range )
        continue;

      ConstantInt *sz = dyn_cast< ConstantInt >( cs.getArgument(1) );
      if( !sz )
        continue;

      uint64_t size = sz->getLimitedValue();
      if( size == 1 || size == 2 || size == 4 || size == 8 )
      {
        Constant *spec = api.getPrivateWriteRange(size);

        Instruction *call = CallInst::Create(spec, cs.getArgument(0));
        call->takeName(old);

        InstInsertPt::After(old) << call;

        old->replaceAllUsesWith(call);
        old->eraseFromParent();

        ++numPrivSpecialized;
        modified = true;
      }
    }
    users.clear();
    users.insert( users.end(),
      read_range->user_begin(), read_range->user_end() );
    for(std::vector<User*>::iterator i=users.begin(), e=users.end(); i!=e; ++i)
    {
      CallSite cs = getCallSite(*i);
      Instruction *old = cs.getInstruction();
      if( !old )
        continue;
      if( cs.getCalledFunction() != read_range )
        continue;

      ConstantInt *sz = dyn_cast< ConstantInt >( cs.getArgument(1) );
      if( !sz )
        continue;

      uint64_t size = sz->getLimitedValue();
      if( size == 1 || size == 2 || size == 4 || size == 8 )
      {
        Constant *spec = api.getPrivateReadRange(size);

        Value *actuals[] = { cs.getArgument(0), cs.getArgument(2) };
        Instruction *call = CallInst::Create(spec, ArrayRef<Value*>(&actuals[0], &actuals[2]) );
        call->takeName(old);

        InstInsertPt::After(old) << call;

        old->replaceAllUsesWith(call);
        old->eraseFromParent();

        ++numPrivSpecialized;
        modified = true;
      }
    }

    return modified;
  }
};




char Postprocess1::ID = 0;
char Postprocess2::ID = 0;
char Postprocess3::ID = 0;

namespace
{
  RegisterPass<Postprocess1> x("spec-priv-postprocess-1",
    "Postprocess RoI for SpecPriv (1): Physically cut edges for Control Speculation");
  RegisterPass<Postprocess2> y("spec-priv-postprocess-2",
    "Postprocess RoI for SpecPriv (2): UO tests; Eliminate, Join, Promote Private Reads/Writes");
  RegisterPass<Postprocess3> z("spec-priv-postprocess-3",
    "Postprocess RoI for SpecPriv (3): Specialize Private Reads/Writes");
}


}
}

