#define DEBUG_TYPE "loop-dominators"

#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Analysis/ControlSpecIterators.h"
#include "liberty/Speculation/LoopDominators.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

// acc := acc U {bb}
static void unionGets(std::vector< ControlSpeculation::LoopBlock > &acc, ControlSpeculation::LoopBlock lb)
{
  // this lower_bound-crap maintains the order invariant
  std::vector< ControlSpeculation::LoopBlock >::iterator i = std::lower_bound( acc.begin(), acc.end(), lb );
  if( i == acc.end() || lb != *i )
    acc.insert(i, lb);
}

bool LoopDom::dom(ControlSpeculation::LoopBlock A, ControlSpeculation::LoopBlock B) const
{
  AdjList::const_iterator i = dt.find(B);
  if( i == dt.end() )
    return false;

  const BBList &dtB = i->second;
  return std::find( dtB.begin(), dtB.end(), A) != dtB.end();
}

LoopDom::dt_iterator LoopDom::dt_begin(ControlSpeculation::LoopBlock bb) const
{
  AdjList::const_iterator i = dt.find(bb);
  if( i == dt.end() )
    return Empty.begin();
  else
    return i->second.begin();
}

LoopDom::dt_iterator LoopDom::dt_end(ControlSpeculation::LoopBlock bb) const
{
  AdjList::const_iterator i = dt.find(bb);
  if( i == dt.end() )
    return Empty.end();
  else
    return i->second.end();
}


ControlSpeculation::LoopBlock LoopDom::idom(ControlSpeculation::LoopBlock bb) const
{
  BB2BB::const_iterator i = idt.find(bb);
  assert( i != idt.end() && "Has no immediate dominator");
  return i->second;
}

void LoopDom::computeDT()
{
  // Initialize
  //  DT[s | s != entry] = all nodes.
  BBList all;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    all.push_back( ControlSpeculation::LoopBlock(*i) );
  all.push_back( ControlSpeculation::LoopBlock::BeforeIteration() );
  all.push_back( ControlSpeculation::LoopBlock::LoopContinue() );

  ControlSpeculation::ExitBlocks exits;
  ctrlspec.getExitBlocks(loop, exits);
  for(unsigned i=0, N=exits.size(); i<N; ++i)
    all.push_back( ControlSpeculation::LoopBlock::LoopExit( exits[i] ) );

  sort(all.begin(), all.end());
  for(BBList::const_iterator i=all.begin(), e=all.end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock lb = *i;
    if( lb.isBeforeIteration() )
      { /* nothing dominates the before-iteration node */ }
    else
      dt[lb] = all;
  }

  // The classic worklist algorithm.
  typedef std::vector< ControlSpeculation::LoopBlock > Fringe;
  Fringe fringe;

  // Start with the entry of one iteration.
  fringe.push_back( ControlSpeculation::LoopBlock::BeforeIteration() );

  while( !fringe.empty() )
  {
    ControlSpeculation::LoopBlock lb = fringe.back();
    fringe.pop_back();

    // Let acc := INTERSECT{ pred in predecessors(bb) } dt(pred)
    BBList acc;
    intersectDTPredecessors(lb, acc);

    // acc := acc UNION { bb }
    unionGets(acc, lb);

    // If changed.
    if( acc != dt[lb] )
    {
      // pd[bb] := acc
      dt[lb].swap( acc );

      // Add successors of bb to the fringe.
      typedef ControlSpeculation::loop_succ_iterator ITER;
      for(ITER i=ctrlspec.succ_begin(loop,lb), e=ctrlspec.succ_end(loop,lb); i!=e; ++i)
      {
        ControlSpeculation::LoopBlock succ =  *i;

        // if not already there.
        if( std::find( fringe.begin(), fringe.end(), succ) == fringe.end() )
          fringe.push_back(succ);
      }
    }
  }
}

void LoopDom::computeIDT()
{
  for(AdjList::const_iterator i=dt.begin(), e=dt.end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock bb = i->first;
    const BBList &domset = i->second;

    BBList::const_iterator j=domset.begin(), z=domset.end();
    assert( j != z && "domset[bb] should not be empty" );
    ControlSpeculation::LoopBlock id = *j;
    for(++j; j!=z; ++j)
    {
      ControlSpeculation::LoopBlock jj = *j;
      if( id == bb || (dom(id,jj) && jj != bb) )
        id = jj;
    }

    idt[ bb ] = id;
  }

  const ControlSpeculation::LoopBlock invalid = ControlSpeculation::LoopBlock();
  idt[ ControlSpeculation::LoopBlock::BeforeIteration() ] = invalid;
}


// acc := INTERSECT PD[i] for all i in predecessor(bb)
void LoopDom::intersectDTPredecessors(ControlSpeculation::LoopBlock bb, BBList &acc)
{
  // Foreach predecessor of bb FROM THE SAME ITERATION!
  bool first = true;
  typedef ControlSpeculation::loop_pred_iterator ITER;
  for(ITER i=ctrlspec.pred_begin(loop,bb), e=ctrlspec.pred_end(loop,bb); i!=e; ++i)
  {
    assert( !bb.isBeforeIteration()
    && "BeforeIteration should have NO predecessors");

    ControlSpeculation::LoopBlock pred = *i;
    const BBList &dt_pred = dt[pred];

    if( first )
    {
      // IH: dt_pred satisfies invariant
      // => acc satisfies invariant.
      acc.insert(acc.begin(), dt_pred.begin(), dt_pred.end());
      first = false;
    }
    else
    {
      // Spec says that result of set_intersection is sorted
      // => acc satisfies invariant.

      // tmp := acc INTERSECT dt(pred)
      BBList tmp;
      std::back_insert_iterator<BBList> ii(tmp);
      std::set_intersection( acc.begin(), acc.end(),
                             dt_pred.begin(), dt_pred.end(),
                             ii );

      // acc := tmp
      acc.swap(tmp);
    }

    // Short circuit:  x INTERSECT emptyset = emptyset.
    if( acc.empty() )
      break;
  }
}




const LoopDom::BBList LoopDom::Empty;

// ----------------- loop post-dominators

// Does A post-dominate B ?
bool LoopPostDom::pdom(ControlSpeculation::LoopBlock A, ControlSpeculation::LoopBlock B) const
{
  AdjList::const_iterator i = pd.find(B);
  if( i == pd.end() )
    return false;

  const BBList &pdB = i->second;
  return std::find( pdB.begin(), pdB.end(), A) != pdB.end();
}

// Inspect post-dominance relation
LoopPostDom::pd_iterator LoopPostDom::pd_begin(ControlSpeculation::LoopBlock bb) const
{
  AdjList::const_iterator i = pd.find(bb);
  if( i == pd.end() )
    return Empty.begin();
  else
    return i->second.begin();
}

LoopPostDom::pd_iterator LoopPostDom::pd_end(ControlSpeculation::LoopBlock bb) const
{
  AdjList::const_iterator i = pd.find(bb);
  if( i == pd.end() )
    return Empty.end();
  else
    return i->second.end();
}

// Inspect post-dominance frontier relation
LoopPostDom::pdf_iterator LoopPostDom::pdf_begin(ControlSpeculation::LoopBlock bb) const
{
  AdjList::const_iterator i = pdf.find(bb);
  if( i == pdf.end() )
    return Empty.begin();
  else
    return i->second.begin();
}

LoopPostDom::pdf_iterator LoopPostDom::pdf_end(ControlSpeculation::LoopBlock bb) const
{
  AdjList::const_iterator i = pdf.find(bb);
  if( i == pdf.end() )
    return Empty.end();
  else
    return i->second.end();
}

// Get the immediate post-dominator for a block
ControlSpeculation::LoopBlock LoopPostDom::ipdom(ControlSpeculation::LoopBlock bb) const
{
  BB2BB::const_iterator i = ipd.find(bb);
  assert( i != ipd.end() && "Has no immediate post-dominator");
  return i->second;
}

void LoopPostDom::printPD(raw_ostream &fout) const
{
  print(fout, pd, "is post-dominated by");
}

void LoopPostDom::printPDF(raw_ostream &fout) const
{
  print(fout, pdf, "is control-dependent on (PDF)");
}

void LoopPostDom::computePD()
{
  ControlSpeculation::ExitBlocks exits;
  ctrlspec.getExitBlocks(loop, exits);

  // Initialize
  //  PD[s | s != exit] = all nodes.
  BBList all_nodes;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
    all_nodes.push_back( ControlSpeculation::LoopBlock( *i ) );
  all_nodes.push_back( ControlSpeculation::LoopBlock::BeforeIteration() );
  all_nodes.push_back( ControlSpeculation::LoopBlock::LoopContinue() );
  for(unsigned i=0, N=exits.size(); i<N; ++i)
    all_nodes.push_back( ControlSpeculation::LoopBlock::LoopExit( exits[i] ) );


  sort(all_nodes.begin(), all_nodes.end());
  for(BBList::const_iterator i=all_nodes.begin(), e=all_nodes.end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock bb = *i;
    if( !bb.isAfterIteration() )
      pd[bb] = all_nodes;
  }

  // The classic worklist algorithm.
  typedef std::vector< ControlSpeculation::LoopBlock > Fringe;
  Fringe fringe;

  // Start with the exit of one iteration.
  fringe.push_back( ControlSpeculation::LoopBlock::LoopContinue() );
  for(unsigned i=0, N=exits.size(); i<N; ++i)
    fringe.push_back( ControlSpeculation::LoopBlock::LoopExit( exits[i] ) );

  while( !fringe.empty() )
  {
    ControlSpeculation::LoopBlock bb = fringe.back();
    fringe.pop_back();

    // Let acc := INTERSECT{ succ in successors(bb) } pd(succ)
    BBList acc;
    intersectPDSuccessors(bb, acc);

    // acc := acc UNION { bb }
    unionGets(acc, bb);

    // If changed.
    if( acc != pd[bb] )
    {
      // pd[bb] := acc
      pd[bb].swap( acc );

      // Add predecessors of bb to the fringe.
      typedef ControlSpeculation::loop_pred_iterator ITER;
      for(ITER i=ctrlspec.pred_begin(loop,bb), e=ctrlspec.pred_end(loop,bb); i!=e; ++i)
      {
        ControlSpeculation::LoopBlock pred = *i;

        // if not already there.
        if( std::find( fringe.begin(), fringe.end(), pred) == fringe.end() )
          fringe.push_back(pred);
      }
    }
  }
}

void LoopPostDom::computeIPD()
{
  for(AdjList::const_iterator i=pd.begin(), e=pd.end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock bb = i->first;
    if( bb.isLoopExit() )
      continue; // The loop exit post dominates nothing in the loop, because such blocks cannot continue, thus are not in the loop.
    const BBList &domset = i->second;

    BBList::const_iterator j=domset.begin(), z=domset.end();
    assert( j != z && "postdomset[bb] should not be empty");
    ControlSpeculation::LoopBlock ip = *j;
    for(++j; j!=z; ++j)
    {
      ControlSpeculation::LoopBlock jj = *j;
      if( ip == bb || (pdom(ip,jj) && jj != bb) )
        ip = jj;
    }

    ipd[ bb ] = ip;
  }

  ControlSpeculation::ExitBlocks exits;
  ctrlspec.getExitBlocks(loop, exits);

  const ControlSpeculation::LoopBlock invalid = ControlSpeculation::LoopBlock();
  ipd[ ControlSpeculation::LoopBlock::LoopContinue() ] = invalid;
  for(unsigned i=0, N=exits.size(); i<N; ++i)
    ipd[ ControlSpeculation::LoopBlock::LoopExit( exits[i] ) ] = invalid;
}

void LoopPostDom::computePDF()
{
  ControlSpeculation::ExitBlocks exits;
  ctrlspec.getExitBlocks(loop, exits);

  computePDF( ControlSpeculation::LoopBlock::LoopContinue() );
  for(unsigned i=0, N=exits.size(); i<N; ++i)
    computePDF( ControlSpeculation::LoopBlock::LoopExit( exits[i] ) );

  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock bb(*i);
    if( ! pdf.count(bb) )
      computePDF( ControlSpeculation::LoopBlock(bb) );
  }
}

void LoopPostDom::computePDF( ControlSpeculation::LoopBlock n )
{
  assert( n.isBeforeIteration()
  ||      n.isAfterIteration()
  ||      loop->contains( n.getBlock() ) );

//  if( pdf.count(n) )
//    return;

  BBList s;

  typedef ControlSpeculation::loop_pred_iterator ITER;
  for(ITER i=ctrlspec.pred_begin(loop,n), e=ctrlspec.pred_end(loop,n); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock y = *i;

    if( ipdom(y) != n )
      s.push_back(y);
  }

  for(BB2BB::const_iterator i=ipd.begin(), e=ipd.end(); i!=e; ++i)
  {
    if( i->second != n )
      continue;

    ControlSpeculation::LoopBlock c = i->first;
    if( c == n )
      continue;

//    assert( ipdom(c) == n );

    computePDF(c);
    for(pdf_iterator j=pdf_begin(c), z=pdf_end(c); j!=z; ++j)
    {
      ControlSpeculation::LoopBlock w = *j;

      if( !pdom(n, w) ) // OLD: ipdom(w) != n )
        s.push_back(w);
    }
  }

  pdf[n].swap(s);
}

// acc := INTERSECT PD[i] for all i in successors(bb)
void LoopPostDom::intersectPDSuccessors(ControlSpeculation::LoopBlock bb, BBList &acc)
{
  // Foreach successor of bb FROM THE SAME ITERATION!
  bool first = true;
  typedef ControlSpeculation::loop_succ_iterator ITER;
  for(ITER i=ctrlspec.succ_begin(loop,bb), e=ctrlspec.succ_end(loop,bb); i!=e; ++i)
  {
    assert( ! bb.isAfterIteration()
    && "AfterIteration should have NO successors");

    ControlSpeculation::LoopBlock succ = *i;
    const BBList &pd_succ = pd[succ];

    if( first )
    {
      // IH: pd_succ satisfies invariant
      // => acc satisfies invariant.
      acc.insert(acc.begin(), pd_succ.begin(), pd_succ.end());
      first = false;
    }
    else
    {
      // Spec says that result of set_intersection is sorted
      // => acc satisfies invariant.

      // tmp := acc INTERSECT pd(succ)
      BBList tmp;
      std::back_insert_iterator<BBList> ii(tmp);
      std::set_intersection( acc.begin(), acc.end(),
                             pd_succ.begin(), pd_succ.end(),
                             ii );

      // acc := tmp
      acc.swap(tmp);
    }

    // Short circuit:  x INTERSECT emptyset = emptyset.
    if( acc.empty() )
      break;
  }
}

void LoopPostDom::printIPD_dot(raw_ostream &fout) const
{
  fout << "digraph ImmediatePostDom {\n";

  for(BB2BB::const_iterator i=ipd.begin(), e=ipd.end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock a = i->first, b = i->second;
    if( a.isAfterIteration() )
      continue;

    fout << "  \"" << a << "\" -> \"" << b << "\";\n";
  }

  fout << "};\n";
}

void LoopPostDom::printIPD(raw_ostream &fout) const
{
  for(BB2BB::const_iterator i=ipd.begin(), e=ipd.end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock a = i->first, b = i->second;
    if( a.isAfterIteration() )
      continue;

    fout << a << " is immediately post-dominated by " << b << '\n';
  }
}

void LoopPostDom::print(raw_ostream &fout, const AdjList &rel, StringRef desc) const
{
  for(AdjList::const_iterator i=rel.begin(), e=rel.end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock bb = i->first;

    fout << bb << ' ' << desc << ":\n";

    for(BBList::const_iterator j=i->second.begin(), z=i->second.end(); j!=z; ++j)
    {
      ControlSpeculation::LoopBlock bb2 = *j;
      fout << "  " << bb2 << '\n';
    }
  }
}


const LoopPostDom::BBList LoopPostDom::Empty;



}
}

