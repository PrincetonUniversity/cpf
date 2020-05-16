// This is a list of LLVM intrinsic functions that the profiler
// handles completely, with all kinds of variances. If the
// program-under-test calls an external
// function which is NOT on this list, then there is a possibility
// that said function might have introduced pointers to an object
// which was not otherwise instrumented.
//
// This list is NOT required for correctness, but is useful for
// precision.  It allows us to discern between undefined behavior
// in the program-under-test and incomplete profile coverage.
//
#ifndef SPECPRIV_PROFILER_MULTI_TYPE_FUNCTIONS_H
#define SPECPRIV_PROFILER_MULTI_TYPE_FUNCTIONS_H
  "llvm.memset",//fill a block of memory with a particular byte value.
  "llvm.experimental.vector.reduce.add",
  "llvm.experimental.vector.reduce.v2.fadd",
  "llvm.experimental.vector.reduce.mul",
  "llvm.experimental.vector.reduce.v2.fmul",
  "llvm.experimental.vector.reduce.and",
  "llvm.experimental.vector.reduce.or",
  "llvm.experimental.vector.reduce.xor",
  "llvm.experimental.vector.reduce.smax",
  "llvm.experimental.vector.reduce.smin",
  "llvm.experimental.vector.reduce.umax",
  "llvm.experimental.vector.reduce.umin",
  "llvm.experimental.vector.reduce.fmax",
  "llvm.experimental.vector.reduce.fmin",
  "llvm.matrix.transpose",
  "llvm.matrix.multiply",
  "llvm.matrix.columnwise.load",
  "llvm.matrix.columnwise.store",
  "llvm.vp.add",
  "llvm.vp.sub",
  "llvm.vp.mul",
  "llvm.vp.sdiv",
  "llvm.vp.udiv",
  "llvm.vp.srem",
  "llvm.vp.urem",
  "llvm.vp.ashr",
  "llvm.vp.lshr",
  "llvm.vp.shl",
  "llvm.vp.or",
  "llvm.vp.and",
  "llvm.vp.xor",
  "llvm.masked.load",
  "llvm.masked.store",
  "llvm.masked.gather",
  "llvm.masked.scatter",
  "llvm.masked.expandload",
  "llvm.masked.compressstor",
// Note that the last entry still has a comma.
#include "./../Analysis/MultiTypePureFun.h"
#endif



