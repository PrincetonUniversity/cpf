#ifndef LLVM_LIBERTY_SPEC_PRIV_LOOP_DOMINATORS_H
#define LLVM_LIBERTY_SPEC_PRIV_LOOP_DOMINATORS_H

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Analysis/ControlSpeculation.h"

#include <vector>

#include "llvm/Analysis/LoopInfo.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

// Compute the dominators of a single iteration
// of a loop from the CFG in conjunction with control
// speculation information.
struct LoopDom
{
  typedef std::vector< ControlSpeculation::LoopBlock > BBList;

  LoopDom(ControlSpeculation &cs, Loop *l) : ctrlspec(cs), loop(l), dt(), idt()
  {
    computeDT();
    computeIDT();
  }

  typedef BBList::const_iterator dt_iterator;

  // Does A dominate B ?
  bool dom(ControlSpeculation::LoopBlock A, ControlSpeculation::LoopBlock B) const;

  // Inspect dominance relation
  /// Enumerate those nodes which dominate bb.
  dt_iterator dt_begin(ControlSpeculation::LoopBlock bb) const;
  dt_iterator dt_end(ControlSpeculation::LoopBlock bb) const;

  // Get the immediate dominator for a block
  ControlSpeculation::LoopBlock idom(ControlSpeculation::LoopBlock bb) const;


  const Loop *getLoop() const { return loop; }
  ControlSpeculation &getControlSpeculation() const { return ctrlspec; }

private:
  ControlSpeculation &ctrlspec;
  Loop *loop;

  // An empty adjacency list.
  static const BBList Empty;

  // For every basic block bb, dt[bb] represents the
  // nodes that dominate bb.
  // Invariant: forall i. pd[i] is sorted ascending by pointer address.
  typedef std::map< ControlSpeculation::LoopBlock, BBList> AdjList;
  AdjList dt;

  // Immediate dominators.
  typedef std::map< ControlSpeculation::LoopBlock, ControlSpeculation::LoopBlock > BB2BB;
  BB2BB idt;

  // Compute the Post-Dominators of the control flow graph
  // of the loop.  This is an intra-iteration post-dominator:
  // the loop backedge and loop exits all 'connect to' an
  // exit node.
  void computeDT();
  void computeIDT();

  // acc := INTERSECT PD[i] for all i in predecessors(bb)
  void intersectDTPredecessors( ControlSpeculation::LoopBlock, BBList &acc);
};


// Compute the post-dominators of a single iteration
// of a loop from the CFG in conjunction with control
// speculation information.
struct LoopPostDom
{
  typedef std::vector< ControlSpeculation::LoopBlock > BBList;

  LoopPostDom(ControlSpeculation &cs, Loop *l) : ctrlspec(cs), loop(l), pd(), pdf(), ipd()
  {
    computePD();
    computeIPD();
    computePDF();
  }

  // Does A post-dominate B ?
  bool pdom(BasicBlock *A, BasicBlock *B) const;

  typedef BBList::const_iterator pd_iterator;
  typedef BBList::const_iterator pdf_iterator;

  // Does A post-dominate B ?
  bool pdom(ControlSpeculation::LoopBlock A, ControlSpeculation::LoopBlock B) const;

  /// Enumerate those nodes which post-dominate bb.
  pd_iterator pd_begin(ControlSpeculation::LoopBlock bb) const;
  pd_iterator pd_end(ControlSpeculation::LoopBlock bb) const;

  // Inspect post-dominance frontier relation
  /// bb is intra-iteration control-dependent on
  /// every block returned by this iterator.
  pdf_iterator pdf_begin(ControlSpeculation::LoopBlock bb) const;
  pdf_iterator pdf_end(ControlSpeculation::LoopBlock bb) const;

  // Get the immediate post-dominator for a block
  ControlSpeculation::LoopBlock ipdom(ControlSpeculation::LoopBlock bb) const;

  void printPD(raw_ostream &fout) const;
  void printPDF(raw_ostream &fout) const;
  void printIPD_dot(raw_ostream &fout) const;
  void printIPD(raw_ostream &fout) const;

  const Loop *getLoop() const { return loop; }
  ControlSpeculation &getControlSpeculation() const { return ctrlspec; }

private:
  ControlSpeculation &ctrlspec;
  Loop *loop;

  // An empty adjacency list.
  static const BBList Empty;

  // For every basic block bb, pd[bb] represents the
  // nodes that post-dominate bb.
  // Invariant: forall i. pd[i] is sorted ascending by pointer address.
  typedef std::map<ControlSpeculation::LoopBlock, BBList> AdjList;
  AdjList pd;

  // For every basic block bb, pdf[bb] represents the
  // post-dominance frontier of bb.
  // No invariant.
  AdjList pdf;

  // Immediate dominators.
  typedef std::map< ControlSpeculation::LoopBlock, ControlSpeculation::LoopBlock> BB2BB;
  BB2BB ipd;

  // Compute the Post-Dominators of the control flow graph
  // of the loop.  This is an intra-iteration post-dominator:
  // the loop backedge and loop exits all 'connect to' an
  // exit node.
  void computePD();
  void computeIPD();
  void computePDF( ControlSpeculation::LoopBlock lb );
  void computePDF();

  // acc := INTERSECT PD[i] for all i in successors(bb)
  void intersectPDSuccessors( ControlSpeculation::LoopBlock lb, BBList &acc);

  void print(raw_ostream &fout, const AdjList &rel, StringRef desc) const;
};

}
}
#endif

