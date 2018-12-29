#ifndef LLVM_LIBERTY_UTIL_SPLIT_BB_H
#define LLVM_LIBERTY_UTIL_SPLIT_BB_H

#include "llvm/IR/Dominators.h"

#include <map>

namespace liberty
{
  using namespace llvm;

  typedef std::map<Instruction*,AllocaInst*> Allocated;

  /// If bb has more than one predecessor,
  /// then duplicate bb so that each copy
  /// has a distinct single predecessor.
  /// You will want to call -mem2reg afterwards.
  void split(std::vector<BasicBlock *> &region);

  /// Same as previous, but allow us to use
  /// a reg->alloca map to reduce unnecessary work.
  /// Although this variant will store defs to the
  /// alloca slots, it will not replace all uses
  /// with loads from allocas.
  void split(std::vector<BasicBlock *> &region, Allocated &allocated);
}

#endif //LLVM_LIBERTY_UTIL_SPLIT_BB_H
