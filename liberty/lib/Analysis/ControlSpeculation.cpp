#define DEBUG_TYPE "ctrlspec"

#include "liberty/Exclusions/Exclusions.h"
#include "liberty/Analysis/CallsiteSearch.h"
#include "liberty/Analysis/Introspection.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/Analysis/CallsiteSearch.h"
#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ControlSpecIterators.h"
//#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/Timer.h"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"


namespace liberty
{
using namespace llvm;

void ControlSpeculation::setLoopOfInterest(const BasicBlock *header)
{
  reachableCache.clear();
  loop_header = header;
}

const BasicBlock *ControlSpeculation::getLoopHeaderOfInterest() const
{
  assert( loop_header && "Someone forgot to ControlSpeculation::setLoopOfInterest()");
  return loop_header;
}

bool ControlSpeculation::LoopBlock::isValid() const
{
  return ! (beforeIteration && loopContinue && loopExit);
}

void ControlSpeculation::LoopBlock::print(raw_ostream &fout) const
{
  if( isBeforeIteration() )
    fout << "BeforeIteration";
  else if( isLoopContinue() )
    fout << "LoopContinue";
  else if( isLoopExit() && bb )
    fout << "LoopExit(" << getBlock()->getName() << ')';
  else if (isLoopExit() )
    fout << "LoopExit(?)";
  else
    fout << "LoopBlock(" << getBlock()->getName() << ')';
}

raw_ostream &operator<<(raw_ostream &fout, const ControlSpeculation::LoopBlock &block)
{
  block.print(fout);
  return fout;
}


bool ControlSpeculation::LoopBlock::operator==(const ControlSpeculation::LoopBlock &other) const
{
  return
     this->beforeIteration == other.beforeIteration
  && this->loopContinue    == other.loopContinue
  && this->loopExit        == other.loopExit
  && this->bb              == other.bb;
}

bool ControlSpeculation::LoopBlock::operator<(const ControlSpeculation::LoopBlock &other) const
{
  if( !this->beforeIteration &&  other.beforeIteration )
    return true;
  if(  this->beforeIteration && !other.beforeIteration )
    return false;

  if( !this->loopContinue &&  other.loopContinue )
    return true;
  if(  this->loopContinue && !other.loopContinue )
    return false;

  if( !this->loopExit &&  other.loopExit )
    return true;
  if(  this->loopExit && !other.loopExit )
    return false;

  return this->bb < other.bb;
}

bool ControlSpeculation::isSpeculativelyDead(const BasicBlock *A, const BasicBlock *B)
{
  const TerminatorInst *term = A->getTerminator();
  for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
  {
    if( term->getSuccessor(sn) == B )
      if( ! isSpeculativelyDead(term,sn) )
        return false;
  }
  return true;
}


bool ControlSpeculation::isSpeculativelyDead(const Instruction *inst)
{
  return isSpeculativelyDead( inst->getParent() );
}

bool ControlSpeculation::isSpeculativelyDead(const Context &context)
{
  const CallsiteContext *cc = context.front();
  if( !cc )
    return false;

  if( isSpeculativelyDead( cc->getLocationWithinParent() ) )
    return true;

  return isSpeculativelyDead( cc->getParent() );
}

bool ControlSpeculation::isSpeculativelyDead(const CtxInst &ci)
{
  return isSpeculativelyDead( ci.getInst() )
  ||     isSpeculativelyDead( ci.getContext() );
}

void ControlSpeculation::getExitingBlocks(Loop *loop, ExitingBlocks &exitingBlocks)
{
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();

    if( mayExit(term,loop) )
      exitingBlocks.push_back(bb);
  }
}


bool  ControlSpeculation::isInfinite(Loop *loop)
{
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();

    if( mayExit(term,loop) )
      return false;
  }

  return true;
}

bool ControlSpeculation::isNotLoop(Loop *loop)
{
  // All preds of the header.
  BasicBlock *header = loop->getHeader();
  for(Value::user_iterator i=header->user_begin(), e=header->user_end(); i!=e; ++i)
  {
    TerminatorInst *term = dyn_cast< TerminatorInst >( &**i );
    if( !term )
      continue;
    BasicBlock *termbb = term->getParent();
    if( ! loop->contains( termbb ) )
      continue;

    if( isSpeculativelyDead(termbb,header) )
      continue;

    return false;
  }

  return true;
}


BasicBlock *ControlSpeculation::getExitingBlock(Loop *loop)
{
  BasicBlock *uniqueExitingBlock = 0;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();

    if( !mayExit(term,loop) )
      continue;

    // bb is an exiting block!
    if( 0 == uniqueExitingBlock )
      uniqueExitingBlock = bb;

    // Not unique?
    else if( uniqueExitingBlock != bb )
      return 0;
  }

  return uniqueExitingBlock;
}

bool ControlSpeculation::mayExit(TerminatorInst *term, Loop *loop)
{
  if( isSpeculativelyDead(term) )
    return false;

  for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
  {
    if( isSpeculativelyDead(term,sn) )
      continue;

    BasicBlock *succ = term->getSuccessor(sn);
    if( ! loop->contains(succ) )
      return true;
  }

  return false;
}

void ControlSpeculation::getExitBlocks(Loop *loop, ControlSpeculation::ExitBlocks &exitBlocks)
{
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();

    if( isSpeculativelyDead(term) )
      continue;

    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      if( isSpeculativelyDead(term,sn) )
        continue;

      BasicBlock *dst = term->getSuccessor(sn);

      if( loop->contains(dst) )
        continue;

      // 'dst' is an exit block
      exitBlocks.push_back( dst );
    }
  }
}


BasicBlock *ControlSpeculation::getUniqueExitBlock(Loop *loop)
{
  BasicBlock *uniqueExitBlock = 0;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    TerminatorInst *term = bb->getTerminator();

    if( isSpeculativelyDead(term) )
      continue;

    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      if( isSpeculativelyDead(term,sn) )
        continue;

      BasicBlock *dst = term->getSuccessor(sn);

      if( loop->contains(dst) )
        continue;

      // dst is an exit block!
      if( 0 == uniqueExitBlock )
        uniqueExitBlock = dst;

      // Not unique?
      else if( uniqueExitBlock != dst )
        return 0;
    }
  }

  return uniqueExitBlock;
}

bool ControlSpeculation::isSpeculativelyUnconditional(const TerminatorInst *term)
{
  const unsigned N = term->getNumSuccessors();
  if( N < 2 )
    return true;

  const BasicBlock *succ = 0;
  for(unsigned sn=0; sn<N; ++sn)
  {
    if( isSpeculativelyDead(term,sn) )
      continue;

    const BasicBlock *ss = term->getSuccessor(sn);
    if( 0 == succ )
      succ = ss;
    else if( succ != ss )
      return false;
  }

  return true;
}

bool ControlSpeculation::phiUseIsSpeculativelyDead(const PHINode *phi, const Instruction *operand)
{
  const unsigned N = phi->getNumIncomingValues();
  for(unsigned i=0; i<N; ++i)
    if( phi->getIncomingValue(i) == operand )
      if( ! phiUseIsSpeculativelyDead(phi, i) )
        return false;

  return true;
}

bool ControlSpeculation::phiUseIsSpeculativelyDead(const PHINode *phi, unsigned operandNumber)
{
  // In the non-speculative case, the PHI node 'phi' uses
  // the operand 'operand' along incoming edge 'operandNumber'.

  // The question now is: is that edge speculatively dead?
  const BasicBlock *predecessor = phi->getIncomingBlock( operandNumber );
  const BasicBlock *successor   = phi->getParent();

  return isSpeculativelyDead(predecessor, successor);
}

ControlSpeculation::succ_iterator ControlSpeculation::succ_begin(BasicBlock *bb)
{
  return BBSuccIterator(bb->getTerminator(),*this);
}
ControlSpeculation::succ_iterator ControlSpeculation::succ_end(BasicBlock *bb)
{
  return BBSuccIterator(bb->getTerminator());
}

ControlSpeculation::pred_iterator ControlSpeculation::pred_begin(BasicBlock *bb)
{
  return BBPredIterator(bb,*this);
}
ControlSpeculation::pred_iterator ControlSpeculation::pred_end(BasicBlock *bb)
{
  return BBPredIterator(bb);
}

ControlSpeculation::loop_succ_iterator ControlSpeculation::succ_begin(Loop *l, LoopBlock lb)
{
  return LoopBBSuccIterator(l, lb, *this);
}

ControlSpeculation::loop_succ_iterator ControlSpeculation::succ_end(Loop *l, LoopBlock lb)
{
  return LoopBBSuccIterator(l, lb);
}

ControlSpeculation::loop_pred_iterator ControlSpeculation::pred_begin(Loop *l, LoopBlock lb)
{
  return LoopBBPredIterator(l, lb, *this);
}

ControlSpeculation::loop_pred_iterator ControlSpeculation::pred_end(Loop *l, LoopBlock lb)
{
  return LoopBBPredIterator(l, lb);
}

bool ControlSpeculation::isReachable(Instruction *src, Instruction *dst, Loop *loop)
{
  BasicBlock *srcbb = src->getParent(), *dstbb = dst->getParent();

  // Same basic block?
  if( srcbb == dstbb )
  {
    // Does src precede dst?
    for(BasicBlock::const_iterator i=srcbb->begin(), e=srcbb->end(); i!=e; ++i)
      if( &*i == dst )
        break;        // dst precedes src
      else if( &*i == src )
        return true;  // src precedes dst

    // There may still be a path from dst
    // to src within loop if there is a
    // nested loop.  Fall through to that test.
  }

  // Different basic blocks.
  return isReachable(srcbb, dstbb, loop);
}

bool ControlSpeculation::isReachable(BasicBlock *src, BasicBlock *dst, Loop *loop)
{
  if( isSpeculativelyDead(src) || isSpeculativelyDead(dst) )
    return false;

  const ReachableKey key(src,dst,loop);
  if( reachableCache.count(key) )
    return reachableCache[key];

  typedef std::vector<BasicBlock*> Fringe;
  typedef std::vector<BasicBlock*> Visited;

  Fringe fringe;
  Visited visited;
  fringe.push_back(src);

  while( !fringe.empty() )
  {
    BasicBlock *n = fringe.back();
    fringe.pop_back();

    Visited::iterator E = visited.end(),
      i=std::lower_bound(visited.begin(), E, n);
    if( i != E && *i == n ) {
      if (n != dst)
        continue;
      else
      // visit of the same block again indicates presence of nested loop.
        return reachableCache[key] = true;
    }

    visited.insert(i,n);

    if( n == dst && visited.size() > 1)
      return reachableCache[key] = true;

    LoopBlock nn(n);
    for(loop_succ_iterator i=succ_begin(loop,nn), e=succ_end(loop,nn); i!=e; ++i)
    {
      LoopBlock succ = *i;
      if( succ.isAfterIteration() )
        continue;

      fringe.push_back( succ.getBlock() );
    }
  }

  DEBUG(errs() << "Found unreachable (intra-iteration) basic blocks: src: "
               << src->getName() << " , dst " << dst->getName() << "\n");
  return reachableCache[key] = false;
}

void ControlSpeculation::dot_block_label(const BasicBlock *bb, raw_ostream &fout) const
{
  fout << bb->getName();
}

void ControlSpeculation::dot_edge_label(const TerminatorInst *term, unsigned sn, raw_ostream &fout) const {}

void ControlSpeculation::to_dot_group_by_loop(Loop *loop, raw_ostream &fout, std::set<BasicBlock*> &already, unsigned depth)
{
  if( isNotLoop(loop)
  || depth >= 2 /* TODO: graphviz breaks when subgraphs nested more than 2 deep */ )
  {
    // Do not print this as a loop, but visit subloops.
    for(Loop::iterator i=loop->begin(), e=loop->end(); i!=e; ++i)
      to_dot_group_by_loop(*i,fout,already, depth);
    return;
  }

  fout << "subgraph \"cluster_Loop_" << loop->getHeader()->getName() << "\" {\n";

  // Color this loop.
  if( isInfinite(loop) )      // infinite loops, pale yellow
    fout << "  style=filled; color=\"#ffffdd\";\n";
  else if( 0 == (depth % 2) ) // even depth, pale red
    fout << "  style=filled; color=\"#ffdddd\";\n";
  else                        // odd depth, pale green
    fout << "  style=filled; color=\"#ddffdd\";\n";

  // Visit each subloop
  for(Loop::iterator i=loop->begin(), e=loop->end(); i!=e; ++i)
    to_dot_group_by_loop(*i,fout,already,depth+1);

  // Put each block into this loop.
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    if( already.count(bb) )
      continue;
    already.insert(bb);

    fout << "  \"" << bb->getName() << "\";\n";
  }

  fout << "}\n";
}

void ControlSpeculation::to_dot(const Function *fcn, LoopInfo &li, raw_ostream &fout)
{
  fout << "digraph \"Spec-CFG\" {\n";
  // Print each block.
  // Color blue if dead.
  for(Function::const_iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
  {
    const BasicBlock *bb = &*i;
    fout << '\"' << bb->getName() << "\" [label=\"";
    dot_block_label(bb, fout);
    fout << "\",shape=box";
    if( isSpeculativelyDead(bb) )
      fout << ",style=filled,color=blue";
    fout << "];\n";
  }

  // Group blocks into loops.
  std::set<BasicBlock*> already_grouped;
  for(LoopInfo::iterator i=li.begin(), e=li.end(); i!=e; ++i)
    to_dot_group_by_loop(*i, fout, already_grouped, 0);

  // Print each control-flow edge.
  // Dashed-and-blue if speculated; blue if its source is unreachable.
  for(Function::const_iterator i=fcn->begin(), e=fcn->end(); i!=e; ++i)
  {
    const BasicBlock *bb = &*i;
    const TerminatorInst *term = bb->getTerminator();
    for(unsigned sn=0, N=term->getNumSuccessors(); sn<N; ++sn)
    {
      const BasicBlock *dest = term->getSuccessor(sn);

      fout << '\"' << bb->getName() << "\" -> \"" << dest->getName() << "\" ";

      fout << "[label=\"";
      dot_edge_label(term,sn,fout);
      fout << "\"";
      if( isSpeculativelyDead(term,sn) )
        fout << ",style=dashed,color=blue";
      else if( isSpeculativelyDead(term) )
        fout << ",color=blue";

      fout << "];\n";
    }
  }
  fout << "}\n";
}

}
