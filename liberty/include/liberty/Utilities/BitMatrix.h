#ifndef LLVM_LIBERTY_UTILITIES_BIT_MATRIX_H
#define LLVM_LIBERTY_UTILITIES_BIT_MATRIX_H

#include "llvm/ADT/BitVector.h"
#include "llvm/Support/raw_ostream.h"

namespace liberty
{
using namespace llvm;

// BitMatrix intended for a dense, asymmetric relation.
struct BitMatrix
{
  BitMatrix(unsigned n) : N(n), bv(n*n) {}

  ~BitMatrix();

  unsigned count() const;

  void set(unsigned row, unsigned col, bool v = true);
  bool test(unsigned row, unsigned col) const;

  int firstSuccessor(unsigned row) const;
  int nextSuccessor(unsigned row, unsigned prev) const;

  void resize(unsigned n);

  void transitive_closure();

  void dump(raw_ostream &fout) const;

private:
  unsigned N;
  BitVector bv;

  unsigned idx(unsigned row, unsigned col) const;
};

}
#endif

