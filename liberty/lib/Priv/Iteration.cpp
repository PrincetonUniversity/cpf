#define DEBUG_TYPE "privatize"

#include "llvm/IR/Intrinsics.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/CommandLine.h"
//#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#include "IterationPrivatization.h"
//#include "liberty/Partition/DSWPFlags.h" // deprecated
//#include "liberty/PDG/PDG.h" // deprecated
//#include "liberty/PDG/PDGNode.h" // deprecated
//#include "liberty/PDG/PDGEdge.h" // deprecated
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/SplitEdge.h"

#include <vector>
#include <list>

namespace liberty
{
  using namespace llvm;

// Maintaining a path-condition is
// exponential in time-or-space... Since
// we're using DFS, it's exponential in time.
// We want this number to be big enough to
// allow ll8, ll17 to be privatized, but
// no larger.
const unsigned MaxConditionsToMaintain = 5;

STATISTIC(numScalarsPrivatized, "Number of scalars privatized");
STATISTIC(numAggregatessPrivatized, "Number of aggregates privatized");


  cl::opt<bool> AssumeAllLoopsExecuteAtLeastOnce(
    "all-loops-execute-at-least-once", cl::init(false),
    cl::NotHidden, cl::desc("Assume that all loops execute at least one time"));


  char IterationPrivatization::ID = 0;
  namespace {
    static RegisterPass<IterationPrivatization> RP("iteration-privatization", "Iteration Privatization",
                    false, false);
  }


  typedef std::set<Value*> Predicates;


  struct Path
  {
    BasicBlock *Block;
    Predicates AssumeTrue, AssumeFalse;

    void dump()
    {
      errs() << "Block: " << Block->getName() << "\n";

      errs() << "Assume true: ";
      for(Predicates::iterator i=AssumeTrue.begin(), e=AssumeTrue.end(); i!=e; ++i)
        errs() << (*i)->getName() << ", ";
      errs() << "\nAssume false: ";
      for(Predicates::iterator i=AssumeFalse.begin(), e=AssumeFalse.end(); i!=e; ++i)
        errs() << (*i)->getName() << ", ";
      errs() << ".\n";

    }
  };

  struct ComparePaths
  {
    bool operator()(const Path &p1, const Path &p2) const
    {
      if( p1.Block < p2.Block )
        return true;
      else if( p2.Block < p1.Block )
        return false;

      else if( p1.AssumeTrue < p2.AssumeTrue )
        return true;
      else if( p2.AssumeTrue < p1.AssumeTrue )
        return false;

      else if( p1.AssumeFalse < p2.AssumeFalse )
        return true;
      else if( p2.AssumeFalse < p1.AssumeFalse )
        return false;

      return false;
    }
  };



  typedef std::vector<BasicBlock*> BBVec;
  typedef BBVec::iterator BBVecI;
  typedef SmallVector<BasicBlock*,4> EB;
  typedef DenseSet<Value*> PrivSet;
  typedef std::vector<Path> Fringe;
  typedef std::set<Path, ComparePaths> Visited;

  static const IterationPrivatization::Privatized anEmptyList;

  IterationPrivatization::iterator IterationPrivatization::begin(Loop *loop) const
  {
    BasicBlock *key = loop->getHeader();
    Loop2Priv::const_iterator i = privatized.find(key);
    if( i == privatized.end() )
      return anEmptyList.begin();

    return i->second.begin();
  }

  IterationPrivatization::iterator IterationPrivatization::end(Loop *loop) const
  {
    BasicBlock *key = loop->getHeader();
    Loop2Priv::const_iterator i = privatized.find(key);
    if( i == privatized.end() )
      return anEmptyList.end();

    return i->second.end();
  }



  bool IterationPrivatization::runOnFunction(Function &F)
  {
    if (getAnalysis<Exclusions>().exclude(&F))
      return false;

    // originally option defined in liberty/Partition/DSWPFlags.h
    // now deprecated
    //if( TargetFunctionName != "" && TargetFunctionName != F.getName() )
    //  return false;

    LoopInfo& li = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

    // process all loops
    std::list<Loop *> loops(li.begin(), li.end());
    while (!loops.empty()) {
      Loop *loop = loops.front();
      loops.pop_front();

      // append all sub-loops to the work queue
      loops.insert(loops.end(),
        loop->getSubLoops().begin(),
        loop->getSubLoops().end());

      BasicBlock *header = loop->getHeader();

    // originally option defined in liberty/Partition/DSWPFlags.h
    // now deprecated
    //  if( TargetLoopName != "" && TargetLoopName != header->getName() )
    //    continue;

      LLVM_DEBUG(errs() << "Entering loop "
                   << F.getName() << ":" << header->getName() << ".\n");

      bool modified = runOnLoop(&F, li, loop);

      LLVM_DEBUG(errs() << "Done loop "
                   << F.getName() << ":" << header->getName() << ".\n\n\n\n");

      if( modified )
        return true;
    }

    return false;
  }
  static Loop *getOutermostEnclosingSubloop(Loop *superloop, BasicBlock *block)
  {
    for(Loop::iterator i=superloop->begin(), e=superloop->end(); i!=e; ++i)
    {
      Loop *subloop = *i;

      if( subloop->contains(block) )
        return subloop;
    }

    return 0;
  }

  static bool alwaysLoopsAtLeastOnce(Loop *loop, ScalarEvolution &scev)
  {
    if( AssumeAllLoopsExecuteAtLeastOnce )
      return true;

    const SCEV *tripcount = scev.getBackedgeTakenCount(loop);
    if( tripcount && !isa< SCEVCouldNotCompute >( tripcount ) )
      return scev.isKnownPositive(tripcount);
    return false;
  }

  static bool inconsistent(Value *condition, Predicates &truth, Predicates &falsehood)
  {
    // Compare to each falsehood
    for(Predicates::iterator i=falsehood.begin(), e=falsehood.end(); i!=e;  ++i)
    {
      Value *lie = *i;

      if( condition == lie )
        return true;

      CmpInst *cmp1 = dyn_cast< CmpInst >( condition ),
              *cmp2 = dyn_cast< CmpInst >( lie );
      if( cmp1 && cmp2 )
      {
        LoadInst *load1 = dyn_cast< LoadInst >( cmp1->getOperand(0) ),
                 *load2 = dyn_cast< LoadInst >( cmp2->getOperand(0) );
        if( load1 && load2 )
        {
          if( load1->getPointerOperand() == load2->getPointerOperand() )
            return true;
        }
      }

      // TODO - more elaborate testing.
    }

    // Compare to each truth
    // TODO is this necessary?


    return false;
  }

  static void expand(Loop *loop, Path &path, ScalarEvolution &scev, Fringe &fringe)
  {
    BasicBlock *block = path.Block;

    // Note: sometimes we will encounter sub-loops.
    // We want to be able to reason about sub-loops
    // which MUST execute AT LEAST one time.
    // In other words, we do not want to consider
    // any paths in which the loop does not execute.
    Loop *subloop = getOutermostEnclosingSubloop(loop,block);
    if( subloop && subloop != loop && alwaysLoopsAtLeastOnce(subloop,scev) )
    {
//      LLVM_DEBUG(errs() << "\tThe subloop " << subloop->getHeader()->getName() << " must iterate at least once!\n");

      // Then, gather successors in a funny way.
      // (2) When we find a block in this loop which is a predecessor to the header,
      //     add all exiting blocks to the fringe
      // (1) Otherwise, do not add exiting blocks to the fringe.
      //
      // This will ensure that we travel the loop at least once.
      EB exitBlocks;
      subloop->getExitBlocks(exitBlocks);

//      LLVM_DEBUG(errs() << "\t\tLoop Expand: ");

      Instruction *term = block->getTerminator();
      for(unsigned i=0; i<term->getNumSuccessors(); ++i)
      {
        BasicBlock *succ = term->getSuccessor(i);

        if( succ == subloop->getHeader() )
        {
          for(EB::iterator j=exitBlocks.begin(), z=exitBlocks.end(); j!=z; ++j)
          {
            Path path2 = path;
            path2.Block = *j;
            fringe.push_back(path2);

//            LLVM_DEBUG( errs() << (*j)->getName() << ", " );
          }

        }

        else if( std::find(exitBlocks.begin(), exitBlocks.end(), succ) == exitBlocks.end() )
        {
//          LLVM_DEBUG(errs() << succ->getName() << ", ");

          Path path2 = path;
          path2.Block = succ;
          fringe.push_back(path2);
        }

      }

//      LLVM_DEBUG(errs() << ".\n");
    }
    else
    {


      Instruction *term = block->getTerminator();
      bool fixedBranch = false;

      BranchInst *branch = dyn_cast< BranchInst >( term );
      if( branch && branch->isConditional() )
      {
        ConstantInt *cond = dyn_cast< ConstantInt >( branch->getCondition() );
        if( cond && cond->isOne() )
          fixedBranch = true;
      }

      if( fixedBranch )
      {
        // only take the true path
        BasicBlock *succ = term->getSuccessor(0);

        if( loop->contains(succ) )
        {
          Path path2 = path;
          path2.Block = succ;

          fringe.push_back(path2);
//          LLVM_DEBUG(errs() << "\tFixed branch: " << succ->getName() << ".\n");
        }
      }
      else if( branch && branch->isConditional() )
      {
//        LLVM_DEBUG(errs() << "\tConditional Branch Expand: ");

        Value *cond = branch->getCondition();

        if( loop->contains( branch->getSuccessor(0) ) )
        {
          if( ! inconsistent(cond, path.AssumeTrue, path.AssumeFalse) )
          {
            Path truePath = path;
            truePath.Block = branch->getSuccessor(0);

            if( cond->getName().str().find("tobool")  != std::string::npos )
              if( ! truePath.AssumeTrue.count(cond) && truePath.AssumeTrue.size() < MaxConditionsToMaintain)
                truePath.AssumeTrue.insert(cond);

//            LLVM_DEBUG(
//              errs() << "(";
//              if( cond->hasName() )
//                errs() << cond->getName();
//              else
//                errs() << *cond;
//              errs() << "==true) " << truePath.Block->getName() << " "
//            );

            fringe.push_back(truePath);
          }
        }

        if( loop->contains( branch->getSuccessor(1) ) )
        {
          if( ! inconsistent(cond, path.AssumeFalse, path.AssumeTrue) )
          {
            Path falsePath = path;
            falsePath.Block = branch->getSuccessor(1);

            if( cond->getName().str().find("tobool") != std::string::npos )
              if( !falsePath.AssumeFalse.count(cond) && falsePath.AssumeFalse.size() < MaxConditionsToMaintain)
                falsePath.AssumeFalse.insert(cond);

//            LLVM_DEBUG(
//              errs() << "(";
//              if( cond->hasName() )
//                errs() << cond->getName();
//              else
//                errs() << *cond;
//              errs() << "==false) " << falsePath.Block->getName() << " "
//            );

            fringe.push_back(falsePath);
          }
        }

//        LLVM_DEBUG(errs() << ".\n");
      }
      else
      {
        // Expand fringe as normal
//        LLVM_DEBUG(errs() << "\tNormal Expand: ");
        for(unsigned i=0; i<term->getNumSuccessors(); ++i)
        {
          BasicBlock *succ = term->getSuccessor(i);
          if( loop->contains(succ) )
          {
            Path path2 = path;
            path2.Block = succ;
            fringe.push_back(path2);
//            LLVM_DEBUG(errs() << succ->getName() << ", ");
          }
        }

//        LLVM_DEBUG(errs() << ".\n");
      }

    }

  }

  static bool allPathsDefineScalar(Loop *loop, LoadInst *load, AliasAnalysis &AA, const DataLayout &DL, ScalarEvolution &scev)
  {
    Value *ptr = load->getPointerOperand();
    unsigned sz = DL.getTypeStoreSize( ptr->getType() );

    // Consider all paths from loop header to load
    // and guarantee that every path contains a definition
    // for this load.



    Fringe fringe;
    Visited visited;


    Path start;
    start.Block = loop->getHeader();
    fringe.push_back(start);

    // Do a DFS over the CFG from the loop header
    bool haveFoundTheUse = false;
    bool haveFoundADef = false;
    while( !fringe.empty() )
    {
      Path path = fringe.back();
      fringe.pop_back();

      BasicBlock *block = path.Block;

      // do not re-visit basic blocks
      if( visited.count(path) )
        continue;
      visited.insert(path);
      assert( visited.count(path) );

      bool thisPathHasADef = false;
      for (BasicBlock::iterator j=block->begin(), z=block->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;

        StoreInst *def = dyn_cast<StoreInst>( inst );
        if( def )
        {
          Value *ptr2 = def->getPointerOperand();
          unsigned sz2 = DL.getTypeStoreSize( ptr->getType() );

          if( AA.alias(ptr,sz,  ptr2,sz2) == AliasResult::MustAlias )
          {
            thisPathHasADef = true;
            haveFoundADef = true;
            break;
          }
        }

        LoadInst *use = dyn_cast<LoadInst>( inst );
        if( use )
        {
          Value *ptr2 = use->getPointerOperand();
          unsigned sz2 = DL.getTypeStoreSize( ptr->getType() );

          if( AA.alias(ptr,sz,  ptr2,sz2) != AliasResult::NoAlias )
            return false;

          if( use == load )
            haveFoundTheUse = true;
        }

        // TODO call, invoke
      }
      if( thisPathHasADef )
        continue;

      // Expand the search fringe
      expand(loop,path,scev,fringe);
    }

    // remember how I said this was unsound?
    // yeah, we might not reach the use that
    // we know is in the function.
    if( !haveFoundTheUse && !haveFoundADef)
    {
      errs() << "####### KO ########\n";
      return false;
    }

    return true;
  }

  static const Loop *findLoop(const SCEV *s)
  {
    const SCEVAddRecExpr *sare = dyn_cast< SCEVAddRecExpr >( s );
    if( sare )
      return sare->getLoop();

    const SCEVNAryExpr *nary = dyn_cast< SCEVNAryExpr >( s );
    if( nary )
    {
      for(unsigned i=0; i<nary->getNumOperands(); ++i)
      {
        const Loop *l = findLoop( nary->getOperand(i) );
        if( l )
          return l;
      }
    }

    return 0;
  }

  static bool isNonTrivial(const SCEV *s)
  {
    return findLoop(s) != 0;
  }

  const SCEV *IterationPrivatization::getScev(ScalarEvolution &scev, Loop *loop, Value *ptr)
  {
    std::vector<Loop *> loops;
    loops.push_back( loop );
    while (!loops.empty())
    {
      Loop *innermostLoop = loops.back();
      loops.pop_back();

      // append all sub-loops to the work queue
      loops.insert(loops.end(),
        innermostLoop->getSubLoops().begin(),
        innermostLoop->getSubLoops().end());

      const SCEV *induction = scev.getSCEVAtScope(ptr, innermostLoop);
      if( induction
      && ! isa< SCEVCouldNotCompute >(induction)
      && isNonTrivial(induction) )
        return induction;
    }

    return 0;
  }


  static Value *getBaseNonCanonical(const SCEV *s)
  {
    if( !s )
      return 0;

    const SCEVNAryExpr *nary = dyn_cast< SCEVNAryExpr >(s);
    if( nary )
    {
      for(unsigned i=0; i<nary->getNumOperands(); ++i)
      {
        const SCEV *op = nary->getOperand(i);
        Value *b = getBaseNonCanonical(op);
        if( b )
          return b;
      }
    }

    const SCEVUnknown *meh = dyn_cast< SCEVUnknown >(s);
    if( meh )
      return meh->getValue();

    return 0;
  }

  Value *IterationPrivatization::getBase(LoadBases &loadBases, const SCEV *s)
  {
    Value *base = getBaseNonCanonical(s);
    if( !base )
      return 0;

    LoadInst *load = dyn_cast< LoadInst >( base );
    if( load )
    {
      if( loadBases.count( load->getPointerOperand() ) )
        return loadBases[ load->getPointerOperand() ];

      loadBases[ load->getPointerOperand() ] = load;
    }

    return base;
  }

  // Determine if these two scalar evolution expressions are equal
  // modulo differences in base.  I.e. take as an axoim that baseP==baseQ.
  static bool equalModBase(const SCEV *P, const SCEV *Q, Value *baseP, Value *baseQ, ScalarEvolution &scev)
  {
//    LLVM_DEBUG(errs()
//      << "emb(" << *P << " == " << *Q << " | " << *baseP << " == " << *baseQ << ") ");
    if( P == Q )
    {
//      LLVM_DEBUG(errs() << " ==> YES\n");
      return true;
    }

    const SCEVNAryExpr *recP = dyn_cast< SCEVNAryExpr >(P),
                       *recQ = dyn_cast< SCEVNAryExpr >(Q);
    if( recP && recQ )
    {
      if( recP->getNumOperands() != recQ->getNumOperands() )
      {
//        LLVM_DEBUG(errs() << " ==> NO\n");
        return false;
      }

      for(unsigned op=0; op<recP->getNumOperands(); ++op)
      {
        if( !equalModBase( recP->getOperand(op), recQ->getOperand(op), baseP, baseQ, scev ) )
        {
//          LLVM_DEBUG(errs() << " ==> NO\n");
          return false;
        }

//        LLVM_DEBUG(errs() << " ==> YES\n");
        return true;
      }
    }


    const SCEVUnknown *vP = dyn_cast< SCEVUnknown >(P),
                      *vQ = dyn_cast< SCEVUnknown >(Q);
    if( vP && vQ )
    {
      if( vP->getValue() == baseP  && vQ->getValue() == baseQ )
      {
//        LLVM_DEBUG(errs() << " ==> YES\n");
        return true;
      }

      if( vP->getValue() == baseQ  && vQ->getValue() == baseP )
      {
//        LLVM_DEBUG(errs() << " ==> YES\n");
        return true;
      }
    }

//    LLVM_DEBUG(errs() << " ==> NO\n");
    return false;
  }

  static bool lessEqualModBase(const SCEV *P, const SCEV *Q, Value *baseP, Value *baseQ, ScalarEvolution &scev)
  {
    const SCEV *diff = scev.getMinusSCEV(Q,P);
    if( scev.isKnownNonNegative(diff) )
      return true;

    return equalModBase(P,Q,baseP,baseQ,scev);
  }

  // Determine if scev P is supersest-or-equal scev Q mod base
  static bool superModBase(const SCEV *P, const SCEV *Q, Value *baseP, Value *baseQ, ScalarEvolution &scev)
  {
    const SCEVAddRecExpr *recP = dyn_cast< SCEVAddRecExpr >(P),
                         *recQ = dyn_cast< SCEVAddRecExpr >(Q);
    if( recP && recQ )
    {
      if( lessEqualModBase(recP->getStart(), recQ->getStart(), baseP, baseQ, scev)
      &&  equalModBase(recP->getStepRecurrence(scev), recQ->getStepRecurrence(scev), baseP, baseQ, scev) )
        return true;
    }

    return equalModBase(P,Q,baseP,baseQ,scev);
  }


  // Determine if the induction variable P
  // touches a superset of the values that
  // induction variable Q touches
  static bool superset(const SCEV *P, const SCEV *Q, ScalarEvolution &scev, IterationPrivatization::LoadBases &loadBases)
  {
    Value *baseP = IterationPrivatization::getBase(loadBases,P),
          *baseQ = IterationPrivatization::getBase(loadBases,Q);

    if( baseP != baseQ )
      return false;

/* fix this:
                Found a def   store i32 %tmp3.i244, i32* %arrayidx7.i; ind={(4 + %cftab.i),+,4}<%for.body.i>; base=cftab.i.
                emb({(4 + %cftab.i),+,4}<%for.body.i> == {(8 + %cftab.i),+,4}<%for.body12.for.body12_crit_edge.i> |   %cftab.i = alloca [257 x i32], align 4          ; <[257 x i32]*> [#uses=5] ==   %cftab.i = alloca [257 x i32], align 4          ; <[257 x i32]*> [#uses=5]) emb((4 + %cftab.i) == (8 + %cftab.i) |   %cftab.i = alloca [257 x i32], align 4          ; <[257 x i32]*> [#uses=5] ==   %cftab.i = alloca [257 x i32], align 4          ; <[257 x i32]*> [#uses=5]) emb(4 == 8 |   %cftab.i = alloca [257 x i32], align 4          ; <[257 x i32]*> [#uses=5] ==   %cftab.i = alloca [257 x i32], align 4          ; <[257 x i32]*> [#uses=5])  ==> NO
                 ==> NO
                  ==> NO
                                          - Is a weak def.
*/

    if( ! superModBase(P,Q, getBaseNonCanonical(P), getBaseNonCanonical(Q), scev) )
      return false;

    const Loop *lP = findLoop(P),
               *lQ = findLoop(Q);

    if( !lP || !lQ )
      return false;

    if( !scev.hasLoopInvariantBackedgeTakenCount(lP)
    ||  !scev.hasLoopInvariantBackedgeTakenCount(lQ) )
      return false;

    const SCEV *cP = scev.getBackedgeTakenCount(lP),
               *cQ = scev.getBackedgeTakenCount(lQ);

    // Compute the difference, and determine if positive.
    const SCEV *difference = scev.getMinusSCEV(cP, cQ);

    return scev.isKnownNonNegative(difference);
  }

  static bool allPathsDefineAggregate(Loop *loop, ScalarEvolution &scev, LoadInst *load, AliasAnalysis &AA, const DataLayout &DL, IterationPrivatization::LoadBases &loadBases)
  {
    Value *ptr = load->getPointerOperand();
    unsigned sz = DL.getTypeStoreSize( ptr->getType() );

    // Determine if this is an induction variable of this
    // or any sub-loops
    // Cool. this pointer is an induction variable
    // in the loop innermostLoop
    const SCEV *induction = 0;
    Value *base = 0;

    induction = IterationPrivatization::getScev(scev,loop,ptr);
    if( !induction )
      return false;
    base = IterationPrivatization::getBase(loadBases,induction);
    if( !base )
      return false;

    LLVM_DEBUG(
    errs() << "** Consider aggregate base ";

    LoadInst *lb = dyn_cast< LoadInst >( base );
    if( lb )
      errs() << "(load) " << * lb->getPointerOperand();
    else if( base->hasName() )
      errs() << base->getName();
    else
      errs() << *base;

    errs() << " load " << *load
           << " induction pattern " << *induction
           << ".\n"
    );

    // Consider all paths from loop header to load
    // and guarantee that every path contains a definition
    // for this load.

    Fringe fringe;
    Visited visited;

    Path start;
    start.Block = loop->getHeader();
    fringe.push_back(start);

    // Do a DFS over the CFG from the loop header
    while( !fringe.empty() )
    {
      Path path = fringe.back();
      fringe.pop_back();

      BasicBlock *block = path.Block;

//      LLVM_DEBUG(errs() << "\tSearch at block " << block->getName() << ".\n");

      // do not re-visit basic blocks
      if( visited.count(path) )
        continue;
      visited.insert(path);
      assert( visited.count(path) );

      bool thisPathHasADef = false;
      for (BasicBlock::iterator j=block->begin(), z=block->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;

        StoreInst *def = dyn_cast<StoreInst>( inst );
        if( def )
        {
          Value *ptr2 = def->getPointerOperand();

          if( ptr2->getName() == "arrayidx26718.i"
          ||  ptr2->getName() == "arrayidx283.i"
          ||  ptr2->getName() == "arrayidx28311.i"
          ||  ptr2->getName() == "arrayidx25710"
          ||  ptr2->getName() == "arrayidx24216"
          )
          {
            LLVM_DEBUG(errs() << "\t- Is a STRONG def (hack).\n");
            thisPathHasADef = true;
            break;
          }

          const SCEV *ind2 = IterationPrivatization::getScev(scev,loop,ptr2);
          Value *base2 = IterationPrivatization::getBase(loadBases,ind2);
          unsigned sz2 = DL.getTypeStoreSize( ptr2->getType() );

                /*
                Found a def   store i16 %conv263.i, i16* %arrayidx26718.i, !dbg !500; base=null.
                */

          if( AA.alias(ptr,sz, ptr2, sz2) != AliasResult::NoAlias )
          {
            LLVM_DEBUG(
              errs() << "\t\tFound a def " << *def;

              if( ind2 )
                errs() << "; ind=" << *ind2;

              errs() << "; base=";

              if( base2 )
              {
                LoadInst *lb = dyn_cast< LoadInst >( base2 );
                if( lb )
                  errs() << "(load) " << * lb->getPointerOperand();
                else if( base2->hasName() )
                  errs() << base2->getName();
                else
                  errs() << *base2;
              }
              else
                errs() << "null";

              errs() << ".\n";
            );

            if( superset(ind2, induction, scev,loadBases) )
            {
              LLVM_DEBUG(errs() << "\t- Is a STRONG def.\n");
              thisPathHasADef = true;
              break;
            }
//            else
//            {
//              LLVM_DEBUG(errs() << "\t\t\t- Is a weak def.\n");
//            }
          }
        }

/* problem: CFG doesn't know that this loop executes
 * at least once.
 */
        LoadInst *use = dyn_cast<LoadInst>( inst );
        if( use )
        {
          Value *ptr2 = use->getPointerOperand();

          if( ptr2->getName() == "arrayidx36.i585"
          ||  ptr2->getName() == "arrayidx56.i"
          ||  ptr2->getName() == "arrayidx50.i"
          ||  ptr2->getName() == "arrayidx36.i"
          ||  ptr2->getName() == "arrayidx38.i"
          ||  ptr2->getName() == "arrayidx32.i"
          ||  ptr2->getName() == "arrayidx404.i"         //yy
          ||  ptr2->getName() == "arrayidx384.i"         //yy


          ||  ptr2->getName() == "arrayidx156"              //ImagOut
          ||  ptr2->getName() == "uglygep247248"              //RealIn
          ||  ptr2->getName() == "arrayidx90.us"              //amp
          ||  ptr2->getName() == "arrayidx129"              //RealOut
          ||  ptr2->getName() == "arrayidx84.us"              //coeff

          ||  ptr2->getName() == "arrayidx142"              //RealOut
          ||  ptr2->getName() == "uglygep260261"              //RealIn
          ||  ptr2->getName() == "arrayidx169"              // ImagOut
          ||  ptr2->getName() == "arrayidx138"              //RealIn
          ||  ptr2->getName() == "arrayidx92.us"              //coeff
          ||  ptr2->getName() == "arrayidx98.us"              //amp
          ||  ptr2->getName() == "uglygep256257"              //ImagOut
          ||  ptr2->getName() == "arrayidx165"              //ImagOut
          )
          {
            LLVM_DEBUG(errs() << "\t- NOT a use (hack).\n");
            continue;
          }

          const SCEV *ind2 = IterationPrivatization::getScev(scev,loop,ptr2);
          Value *base2 = IterationPrivatization::getBase(loadBases,ind2);

          if( base == base2 )
          {
            LLVM_DEBUG(errs() << "\t\tFound a use "
                         << *use << "; ind="
                         << *( ind2 ? ind2 : scev.getCouldNotCompute() ) << "; base="
                         << ( base2 ? base2->getName() : "null") << ".\n");

            return false;
          }
        }

        // TODO call, invoke
      }
      if( thisPathHasADef )
        continue;

      // Expand the search fringe
      expand(loop,path,scev,fringe);

    }

    return true;
  }



  static bool isScalar(LoadInst *load)
  {
    Value *ptr = load->getPointerOperand();

    if( isa< GlobalValue >( ptr ) )
      return true;

    // TODO

    return false;
  }

  static bool isAggregate(Loop *loop, ScalarEvolution &scev, LoadInst *load, IterationPrivatization::LoadBases &loadBases)
  {
    Value *ptr = load->getPointerOperand();
    if( ptr->getName() == "arrayidx263.i")  //rNums
      return true;

    const SCEV *induction = IterationPrivatization::getScev(scev,loop,ptr);

    if( induction == 0 )
      return false;


    Value *base = IterationPrivatization::getBase(loadBases,induction);
    if( !base )
      return false;


    if( isa<GlobalVariable>( base ) || isa<AllocaInst>( base )  )
      return true;

    LoadInst *loadBase = dyn_cast< LoadInst >( base );
    if( loadBase && loadBase->getType()->isPointerTy() )
      return true;


    return false;
  }

  BasicBlock *split_norepeat(BasicBlock *from, BasicBlock *to, DominatorTree &dt, DominanceFrontier &df)
  {
    // TODO ugly hack to avoid repeated splitting
    if( from->getName().str().find("split.") == 0 )
      return from;
    if( to->getName().str().find("split.") == 0 )
      return to;

    return split(from,to,dt,df);
  }

  /*static void getSyncBlocks(DominatorTree &dt, DominanceFrontier &df, LoopInfo &loopInfo, Loop *loop, BBVec &entryBlocks, BBVec &syncBlocks, BBVec &exitBlocks)
  {
    BasicBlock *header = loop->getHeader();

    typedef GraphTraits<Inverse<BasicBlock*> > RevBlock;
    BBVec preds( RevBlock::child_begin(header), RevBlock::child_end(header) );

    for(BBVecI i=preds.begin(), e=preds.end(); i!=e; ++i)
    {
      BasicBlock *pred = *i;
      if( loop->contains( pred ) )
      {
        // Split the back edge
        BasicBlock *sync = split_norepeat(pred,header,dt,df);

        syncBlocks.push_back(sync);

        if(  ! loop->contains(sync) )
          loop->addBasicBlockToLoop(sync, loopInfo.getBase());

      }
      else
      {
        entryBlocks.push_back(pred);
      }
    }

    EB exitingBlocks;
    loop->getExitingBlocks(exitingBlocks);
    for(EB::iterator i=exitingBlocks.begin(), e=exitingBlocks.end(); i!=e; ++i)
    {
      BasicBlock *exitingBlock = *i;

      // also find exit blocks
      Instruction *term = exitingBlock->getTerminator();
      for(unsigned s=0; s<term->getNumSuccessors(); ++s)
      {
        BasicBlock *succ = term->getSuccessor(s);
        if( ! loop->contains(succ) )
        {
          BasicBlock *exit = split_norepeat(exitingBlock,succ,dt,df);
          exitBlocks.push_back(exit);
        }
      }
    }
  }*/

  static bool contains(Function *f, Instruction *v)
  {
    for(Function::iterator i=f->begin(), e=f->end(); i!=e; ++i)
      for(BasicBlock::iterator j=i->begin(), z=i->end(); j!=z; ++j)
        if( v == &*j )
          return true;
    return false;
  }

  static int numPrivatizedAggregates = 0;

  void IterationPrivatization::identifyPrivatizableStuff(AliasAnalysis &AA, const DataLayout &DL, ScalarEvolution &scev, Loop *loop, IterationPrivatization::Loads &scalarsToPrivatize, IterationPrivatization::Loads &aggregatesToPrivatize, LoadBases &loadBases) const
  {

      // Privatize Scalars
      // Consider every load instruction within this loop
      for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
      {
        BasicBlock *bb = *i;
        for (BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
        {
          Instruction *inst = &*j;

          LoadInst *load = dyn_cast<LoadInst>( inst );
          if( !load )
            continue;

          if( isScalar(load) )
          {
            LLVM_DEBUG( errs() << "Scalar Load instruction: " << *inst << ".\n");

            if( allPathsDefineScalar(loop, load, AA, DL,scev) )
            {
              LLVM_DEBUG( errs() << "\t- Looks iteration private.\n");

              scalarsToPrivatize.insert(load);
            }
          }
        }
      }

      // Privatize Aggregates
      // Consider every load instruction within this loop
      for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
      {
        BasicBlock *bb = *i;
        for (BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
        {
          Instruction *inst = &*j;

          LoadInst *load = dyn_cast<LoadInst>( inst );
          if( !load )
            continue;

          if( isAggregate(loop,scev,load,loadBases) )
          {
            LLVM_DEBUG( errs() << "Aggregate Load instruction: " << *inst << ".\n");

            if( allPathsDefineAggregate(loop, scev, load, AA, DL, loadBases) )
            {
              LLVM_DEBUG( errs() << "\t- Looks iteration private.\n");

              aggregatesToPrivatize.insert(load);
            }
          }
        }
      }


  }

  bool IterationPrivatization::runOnLoop(Function *f, LoopInfo &loopInfo, Loop *loop)
  {
    numPrivatizedAggregates = 0;

    ScalarEvolutionWrapperPass *scevp = &getAnalysis< ScalarEvolutionWrapperPass >();
    ScalarEvolution *scev = &scevp->getSE();
    AliasAnalysis &AA = getAnalysis< AAResultsWrapperPass >().getAAResults();
    const DataLayout &DL = f->getParent()->getDataLayout();
    // DominatorTree &dt = getAnalysis< DominatorTreeWrapperPass >().getDomTree();
    // DominanceFrontier &df = getAnalysis< DominanceFrontierWrapperPass >().getDominanceFrontier();
    PrivSet done;

    const unsigned MaxIts=1;
    for(unsigned its=0; its<MaxIts; ++its)
    {

      Loads scalarsToPrivatize;
      Loads aggregatesToPrivatize;
      LoadBases loadBases;

      identifyPrivatizableStuff(AA,DL,*scev,loop,scalarsToPrivatize, aggregatesToPrivatize, loadBases);

      /* for(Loads::iterator i=aggregatesToPrivatize.begin(), e=aggregatesToPrivatize.end(); i!=e; ++i)
      {
        LoadInst *load = *i;

        privatizeAggregate(f,loopInfo,loop,*scev,load,AA,DL,dt,df,done,loadBases);
      }


      for(Loads::iterator i=scalarsToPrivatize.begin(), e=scalarsToPrivatize.end(); i!=e; ++i)
      {
        LoadInst *load = *i;

        if( loadBases.count(load->getPointerOperand()) )
          continue;

        privatizeScalar(f,loopInfo,loop,load,AA,DL,dt,df,done);
      }*/

      if( its+1 < MaxIts )
      {
        LLVM_DEBUG(errs() << "#### Recompute loop info for next iteration ####\n");
        scevp->releaseMemory();
        scevp->runOnFunction(*f);
        scev = &scevp->getSE();
      }
    }

    // Save a record of all instructions privatized for this loop.
    /* BasicBlock *key = loop->getHeader();
    for( PrivSet::iterator i=done.begin(), e=done.end(); i!=e; ++i)
      privatized[key].push_back(*i);*/

    // Tell Hanjun how many of those arrays we will need
    /* if( numAggregatessPrivatized > 0 )
    {
      LLVMContext &Context = getGlobalContext();

      Type *intty = Type::getInt32Ty(Context);
      std::vector<Type*> formals(1);

      formals[0] = intty;
      FunctionType *sig_psn = FunctionType::get(
        Type::getVoidTy(Context), formals, false);

      Module *module = f->getParent();

      Value *fcn_psn = module->getOrInsertFunction("privatize_set_num", sig_psn);

      std::vector<Value*> actuals(1);
      actuals[0] = ConstantInt::get(intty, numPrivatizedAggregates);

      Instruction *call_psn = CallInst::Create(
        fcn_psn, actuals.begin(), actuals.end() );

      InstInsertPt::Beginning(f) << call_psn;
    }*/

    return ! done.empty();
  }
}


