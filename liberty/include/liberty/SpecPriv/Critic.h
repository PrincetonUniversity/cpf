#ifndef LLVM_LIBERTY_CRITICISMS_H
#define LLVM_LIBERTY_CRITICISMS_H

#include "liberty/SpecPriv/PDG.h"

#include <set>
#include <memory>
#include <tuple>

namespace liberty {
using namespace llvm;
using namespace SpecPriv;

// Criticism is a PDG edge with a boolean value to differentiate loop-carried
// from intra-iteration edges. Also specify type of dep (mem/reg/ctrl)
typedef std::tuple<Vertices::ID, Vertices::ID, bool, DepType> Criticism;

typedef std::set<Criticism> Criticisms;

struct CriticRes {
  Criticisms criticisms;
  int expSpeedup;
};

class Critic {
public:
  static Criticisms getAllCriticisms(const PDG &pdg);
  static void printCriticisms(raw_ostream &fout, Criticisms &criticisms,
                              const PDG &pdg);
  virtual CriticRes getCriticisms(const PDG &pdg) = 0;
  virtual StringRef getCriticName() const = 0;
};

class DOALLCritic : public Critic {
public:
  CriticRes getCriticisms(const PDG &pdg);
  StringRef getCriticName() const {return "doall-critic";};
};

} // namespace liberty

#endif
