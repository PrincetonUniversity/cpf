#ifndef LLVM_LIBERTY_SPEC_PRIV_PIECES_H
#define LLVM_LIBERTY_SPEC_PRIV_PIECES_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "liberty/Analysis/LoopAA.h"

#include <map>
#include <list>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

enum CtxType { Ctx_Top=0, Ctx_Fcn, Ctx_Loop };
struct Ctx : FoldingSetNode
{
  Ctx(CtxType t = Ctx_Top, const Ctx *p = 0) : type(t), parent(p), fcn(0), header(0), depth(0) {};

  CtxType type;
  const Ctx *parent;
  const Function *fcn;
  const BasicBlock *header;
  unsigned depth;

  virtual void Profile(FoldingSetNodeID &) const;

  // Perform a loose test.
  // If every node from 'cc' exists in 'this',
  // in the same order, return true.
  // Ignores any additional nodes in 'this';
  // 'cc' is not necessarily contiguous in 'this.
  bool matches(const Ctx *cc) const;

  // Assuming that this matches cc,
  // determine if cc is within a subloop of cc.
  bool isWithinSubloopOf(const Ctx *cc) const;

  // Does this context reference a given value?
  bool referencesValue(const Value *v) const;

  void print_step(raw_ostream &) const;
  void print(raw_ostream &) const;

  // Compares a single element of context.
  // does not recur to parent contexts.
  bool step_equal(const Ctx *other) const;

  const Function *getFcn() const;

  // Find the innermost function invocation which contains
  // this context.
  const Ctx *getFcnContext() const;
};

raw_ostream &operator<<(raw_ostream &, const Ctx &);

// TODO: unmanaged objects
enum AUType { AU_Unknown=0, AU_Undefined, AU_IO, AU_Null, AU_Constant, AU_Global, AU_Stack, AU_Heap };
struct AU : FoldingSetNode
{
  AU(AUType t) : type(t), value(0), ctx(0) {}

  AUType type;
  const Value *value;
  const Ctx *ctx;

  virtual void Profile(FoldingSetNodeID &) const;

  bool operator==(const AU &other) const
  {
    return type == other.type
    &&     value == other.value
    &&     (ctx->matches(other.ctx) || other.ctx->matches(ctx));
 //   &&     ctx == other.ctx;
  }

  void print(raw_ostream &) const;

  bool referencesValue(const Value *v) const;
};

typedef std::vector<AU *> AUs;

raw_ostream &operator<<(raw_ostream &, const AU &);

struct Ptr
{
  Ptr(AU *a, unsigned o, unsigned f) : au(a), offset(o), frequency(f) {}

  AU *au;
  unsigned offset;
  unsigned frequency;

  bool referencesValue(const Value *v) const;
};
typedef std::vector<Ptr> Ptrs;

struct Int
{
  Int(unsigned u, unsigned f) : value(u), frequency(f) {}

  unsigned value;
  unsigned frequency;
};
typedef std::vector<Int> Ints;


/// Provides a strict weak ordering for
/// AUs | au->type == Stack/Heap
/// which is repeatable across invocations
/// (does not rely on pointer addresses).
/// This is slow, but useful for debugging
struct RepeatableOrderForAUs
{
  bool operator()(const AU * a, const AU * b);
private:
  bool compareContexts(const Ctx *a, const Ctx *b);
};

}
}
#endif

