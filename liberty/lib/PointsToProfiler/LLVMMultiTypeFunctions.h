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
  "llvm.memset",
  "llvm.sqrt",
  "llvm.powi",
  "llvm.sin",
  "llvm.cos",
  "llvm.pow",
  "llvm.exp",
  "llvm.exp2",
  "llvm.log",
  "llvm.log10",
  "llvm.log2",
  "llvm.fma",
  "llvm.fabs",
  "llvm.minnum",
  "llvm.maxnum",
  "llvm.maximum",
  "llvm.copysign",
  "llvm.floor",
  "llvm.ceil",
  "llvm.trunc",
  "llvm.rint",
  "llvm.nearbyint",
  "llvm.round",
  "llvm.lround",
  "llvm.llround",
  "llvm.lrint",
  "llvm.llrint",
  "llvm.bitreverse",
  "llvm.bswap",
  "llvm.ctpop",
  "llvm.ctlz",
  "llvm.cttz",
  "llvm.fshl",
  "llvm.fshr",
  "llvm.sadd.with.overflow",
  "llvm.uadd.with.overflow",
  "llvm.ssub.with.overflow",
  "llvm.usub.with.overflow",
  "llvm.smul.with.overflow",
  "llvm.umul.with.overflow",
  "llvm.sadd.sat",
  "llvm.uadd.sat",
  "llvm.ssub.sat",
  "llvm.usub.sat",
  "llvm.smul.fix",
  "llvm.umul.fix",
  "llvm.smul.fix.sat",
  "llvm.umul.fix.sat",
  "llvm.sdiv.fix",
  "llvm.udiv.fix",
  "llvm.sdiv.fix.sat",
  "llvm.udiv.fix.sat",
  "llvm.canonicalize",
  "llvm.fmuladd",
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
  "llvm.is.constant",
// Note that the last entry still has a comma.
#endif



