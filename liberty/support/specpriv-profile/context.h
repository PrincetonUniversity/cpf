#ifndef SPECPRIV_CONTEXT_H
#define SPECPRIV_CONTEXT_H

#include <cstdlib>
#include <stdint.h>
#include <ostream>
#include <vector>

#include "holder.h"

// The active caller/loop context.
struct Context;
typedef Holder<Context> CtxHolder;

enum CtxType { Top=0, Function, Loop };
struct Context : public RefCount
{
  Context(CtxType t = Top) : RefCount(), type(t), name(0), parent(0) {}

  CtxType type;
  const char *name;
  CtxHolder parent;

  CtxHolder innermostFunction();
  CtxHolder findCommon(const CtxHolder &other);

  bool operator==(const Context &other) const;
  bool operator<(const Context &other) const;

  void print(std::ostream &fout) const;

  // This will actually contain only allocation units.
  // However, that creates a static cycle among types :(
  typedef std::vector<Holder< RefCount > > AUs;
  AUs aus;
};

std::ostream &operator<<(std::ostream &fout, const CtxHolder &ctx);

#endif

