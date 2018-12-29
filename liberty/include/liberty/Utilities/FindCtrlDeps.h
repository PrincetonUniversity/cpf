#ifndef FIND_CTRL_DEPS_H
#define FIND_CTRL_DEPS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"

#include <vector>

namespace liberty {

  template<class BB, class Iterator> std::vector<BB>
  findCtrlDepsImpl(BB,
                   const Iterator &B,
                   const Iterator &E,
                   const llvm::PostDominatorTree &DT);

  std::vector<const llvm::BasicBlock *>
  findCtrlDeps(const llvm::BasicBlock *bb,
               const llvm::Loop::block_iterator &B,
               const llvm::Loop::block_iterator &E,
               const llvm::PostDominatorTree &DT) {
    return findCtrlDepsImpl(bb, B, E, DT);
  }

  std::vector<const llvm::BasicBlock *>
  findCtrlDeps(const llvm::BasicBlock *bb,
               const llvm::Function::iterator &B,
               const llvm::Function::iterator &E,
               const llvm::PostDominatorTree &DT) {
    return findCtrlDepsImpl(bb, B, E, DT);
  }

  std::vector<llvm::BasicBlock *>
  findCtrlDeps(llvm::BasicBlock *bb,
               const llvm::Loop::block_iterator &B,
               const llvm::Loop::block_iterator &E,
               const llvm::PostDominatorTree &DT) {
    return findCtrlDepsImpl(bb, B, E, DT);
  }

  std::vector<llvm::BasicBlock *>
  findCtrlDeps(llvm::BasicBlock *bb,
               const llvm::Function::iterator &B,
               const llvm::Function::iterator &E,
               const llvm::PostDominatorTree &DT) {
    return findCtrlDepsImpl(bb, B, E, DT);
  }
}

#endif /* FIND_CTRL_DEPS_H */
