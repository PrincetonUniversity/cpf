// This file declares four types of iterators, which operate on the SPECULATIVE CFG.
//
// - BBSuccIterator: iterate over the successors of a basic block.
// - BBPredIterator: iterate over the predecessors of a basic block.
// - LoopBBSuccIterator: iterate over the successors of a basic block within a single iteration of a loop.
// - LoopBBPredIterator: iterate over the predecessors of a basic block within a single iteration of a loop.
//
// Don't instantiate these directly; instead use
// methods ControlSpeculation::pred_begin, ControlSpeculation::succ_begin, etc

#ifndef LLVM_LIBERTY_SPEC_PRIV_CTRL_SPEC_ITERATORS_H
#define LLVM_LIBERTY_SPEC_PRIV_CTRL_SPEC_ITERATORS_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "liberty/Speculation/LoopDominators.h"

#include <set>

namespace liberty
{
using namespace llvm;

struct ControlSpeculation;

// An iterator over the successors of a block
struct BBSuccIterator
{
  // Construct a begin iterator
  BBSuccIterator(Instruction *ti, ControlSpeculation &cs);

  // Construnct an end iterator
  BBSuccIterator(Instruction *ti);

  BasicBlock *operator*() const;
  BasicBlock *operator->() const;

  BBSuccIterator &operator++();

  bool operator!=(const BBSuccIterator &other) const;
  bool operator==(const BBSuccIterator &other) const;

private:
  ControlSpeculation *ctrlspec;
  Instruction *term;
  unsigned sn;

  void skipDead();
};

// An iterator over the predecessors of a block.
struct BBPredIterator
{
  // Construct a begin iterator
  BBPredIterator(BasicBlock *bb, ControlSpeculation &cs);

  // Construnct an end iterator
  BBPredIterator(BasicBlock *bb);

  BasicBlock *operator*() const;
  BasicBlock *operator->() const;

  BBPredIterator &operator++();

  bool operator!=(const BBPredIterator &other) const;
  bool operator==(const BBPredIterator &other) const;

private:
  ControlSpeculation *ctrlspec;
  BasicBlock *succ;
  pred_iterator piter;

  void skipDead();
};


// An iterator over the successors of a block in LOOP CFG
struct LoopBBSuccIterator
{
  // Construct a begin iterator
  LoopBBSuccIterator(Loop *l, ControlSpeculation::LoopBlock lb, ControlSpeculation &cs);

  // Construnct an end iterator
  LoopBBSuccIterator(Loop *l, ControlSpeculation::LoopBlock lb);

  ControlSpeculation::LoopBlock operator*() const;
  ControlSpeculation::LoopBlock operator->() const;

  LoopBBSuccIterator &operator++();

  bool operator!=(const LoopBBSuccIterator &other) const;
  bool operator==(const LoopBBSuccIterator &other) const;

private:
  Loop *loop;
  ControlSpeculation *ctrlspec;
  ControlSpeculation::LoopBlock pred;
  unsigned sn;

  void skipDead();
};

// An iterator over the predecessors of a block in LOOP CFG.
struct LoopBBPredIterator
{
  // Construct a begin iterator
  LoopBBPredIterator(Loop *l, ControlSpeculation::LoopBlock lb, ControlSpeculation &cs);

  // Construnct an end iterator
  LoopBBPredIterator(Loop *l, ControlSpeculation::LoopBlock lb);

  ControlSpeculation::LoopBlock operator*() const;
  ControlSpeculation::LoopBlock operator->() const;

  LoopBBPredIterator &operator++();

  bool operator!=(const LoopBBPredIterator &other) const;
  bool operator==(const LoopBBPredIterator &other) const;

private:
  Loop *loop;
  ControlSpeculation *ctrlspec;
  ControlSpeculation::LoopBlock succ;
  unsigned pn;
  typedef std::vector<ControlSpeculation::LoopBlock> LBList;
  LBList list;

  bool isEndIterator() const;
};


}

#endif

