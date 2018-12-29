#ifndef FIND_UNDERLYING_OBJECTS_H
#define FIND_UNDERLYING_OBJECTS_H

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/DenseSet.h"

#include <set>

namespace liberty {
  typedef llvm::DenseSet<const llvm::Value *> ObjectSet;
  /// TODO: replace this with GetUnderlyingObjects().
  void findUnderlyingObjects(const llvm::Value *value, ObjectSet &values);

  typedef std::set<const llvm::Value *> UO;
  /// Like the previous, but handles PHI and SELECT, and uses
  /// a more appropriate data structure.
  void GetUnderlyingObjects(const llvm::Value *ptr, UO &uo, const llvm::DataLayout &DL);

  /// Optionally, collect those objects found before/after a PHI
  /// node into separate collections.
  void GetUnderlyingObjects(const llvm::Value *ptr, UO &beforePHI, UO &afterPHI, const llvm::DataLayout &DL, bool isAfterPHI=false);
}

#endif /* FIND_UNDERLYING_OBJECTS_H */

