#define DEBUG_TYPE "critic"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/SpecPriv/Critic.h"
#include "liberty/SpecPriv/PDG.h"

#define DEFAULT_THREADS 24

namespace liberty {
using namespace llvm;

// produce all the possible criticisms
Criticisms Critic::getAllCriticisms(const PDG &pdg) {
  PartialEdgeSet E = pdg.getE();
  Criticisms criticisms;

  for (unsigned i = 0, N = E.numVertices(); i < N; ++i) {
    for (PartialEdgeSet::iterator j = E.successor_begin(i),
                                  z = E.successor_end(i);
         j != z; ++j) {
      Vertices::ID dst = *j;

      if (pdg.hasEdge(i, dst)) {

        DEBUG(errs() << "  Found new edge(s) from " << *pdg.getV().get(i)
                     << " to " << *pdg.getV().get(dst) << '\n');

        if (pdg.hasLoopCarriedCtrlEdge(i, dst))
          criticisms.insert(std::make_tuple(i, dst, true, DepType::Ctrl));
        if (pdg.hasIntraIterationCtrlEdge(i, dst))
          criticisms.insert(std::make_tuple(i, dst, false, DepType::Ctrl));

        if (pdg.hasLoopCarriedMemEdge(i, dst))
          criticisms.insert(std::make_tuple(i, dst, true, DepType::Mem));
        if (pdg.hasIntraIterationMemEdge(i, dst))
          criticisms.insert(std::make_tuple(i, dst, false, DepType::Mem));

        if (pdg.hasLoopCarriedRegEdge(i, dst))
          criticisms.insert(std::make_tuple(i, dst, true, DepType::Reg));
        if (pdg.hasIntraIterationRegEdge(i, dst))
          criticisms.insert(std::make_tuple(i, dst, false, DepType::Reg));
      }
    }
  }
  return criticisms;
}

void Critic::printCriticisms(raw_ostream &fout, Criticisms &cs, const PDG &pdg) {
  fout << "-===============================================================-\n";
  fout << "Printing Criticisms\n";
  for (const auto &c : cs) {
    Vertices::ID v, w;
    bool loopCarried;
    DepType dt;
    std::tie(v, w, loopCarried, dt) = c;
    fout << *pdg.getV().get(v) << " -> " << *pdg.getV().get(w)
         << " (loopCarried=" << loopCarried << ")\n";
  }
  fout << "-===============================================================-\n";
}

CriticRes DOALLCritic::getCriticisms(const PDG &pdg) {
  // Produce criticisms using the applicability guard of DOALL
  DEBUG(errs() << "Begin criticisms generation for DOALL critic\n");

  CriticRes res;
  PartialEdgeSet E = pdg.getE();

  for (unsigned i = 0, N = E.numVertices(); i < N; ++i) {
    for (PartialEdgeSet::iterator j = E.successor_begin(i),
                                  z = E.successor_end(i);
         j != z; ++j) {
      Vertices::ID dst = *j;

      if (pdg.hasLoopCarriedEdge(i, dst)) {

        DEBUG(
            errs() << "  Found new DOALL criticism: loop-carried edge(s) from "
                   << *pdg.getV().get(i) << " to " << *pdg.getV().get(dst)
                   << '\n');

        if (pdg.hasLoopCarriedCtrlEdge(i, dst))
          res.criticisms.insert(std::make_tuple(i, dst, true, DepType::Ctrl));

        if (pdg.hasLoopCarriedMemEdge(i, dst))
          res.criticisms.insert(std::make_tuple(i, dst, true, DepType::Mem));

        if (pdg.hasLoopCarriedRegEdge(i, dst))
          res.criticisms.insert(std::make_tuple(i, dst, true, DepType::Reg));

        if (int c = pdg.removableLoopCarriedEdge(i, dst)) {
          if (c == -1) {
            // criticism cannot be remedied. Abort
            res.expSpeedup = -1;
            return res;
          }
        }
      }
    }
  }
  res.expSpeedup = DEFAULT_THREADS;
  return res;
}

} // namespace liberty
