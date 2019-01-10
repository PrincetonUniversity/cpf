#define DEBUG_TYPE "critic"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/SpecPriv/Critic.h"

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

void Critic::complainAboutEdge(CriticRes &res, const PDG &pdg, Vertices::ID src,
                               Vertices::ID dst, bool loopCarried) {
  if (loopCarried) {
    if (pdg.hasLoopCarriedCtrlEdge(src, dst))
      res.criticisms.insert(
          std::make_tuple(src, dst, loopCarried, DepType::Ctrl));

    if (pdg.hasLoopCarriedMemEdge(src, dst))
      res.criticisms.insert(
          std::make_tuple(src, dst, loopCarried, DepType::Mem));

    if (pdg.hasLoopCarriedRegEdge(src, dst))
      res.criticisms.insert(
          std::make_tuple(src, dst, loopCarried, DepType::Reg));
  } else {
    if (pdg.hasIntraIterationCtrlEdge(src, dst))
      res.criticisms.insert(
          std::make_tuple(src, dst, loopCarried, DepType::Ctrl));

    if (pdg.hasIntraIterationMemEdge(src, dst))
      res.criticisms.insert(
          std::make_tuple(src, dst, loopCarried, DepType::Mem));

    if (pdg.hasIntraIterationRegEdge(src, dst))
      res.criticisms.insert(
          std::make_tuple(src, dst, loopCarried, DepType::Reg));
  }
}

std::unique_ptr<ParallelizationPlan>
Critic::getPipelineStrategy(PDG &pdg) {
  // get Pipeline stages
  std::unique_ptr<ParallelizationPlan> ps =
      std::unique_ptr<ParallelizationPlan>(new ParallelizationPlan());

  SCCs sccs(pdg);
  sccs.recompute(pdg);
  SCCs::markSequentialSCCs(pdg, sccs);
  Loop *loop = pdg.getV().getLoop();
  bool success = Pipeline::suggest(loop, pdg, sccs, *perf, *ps, threadBudget);

  // there should be a parallelization plan given that all the criticisms are
  // addressible
  assert(success);

  return ps;
}

long Critic::getExpPipelineSpeedup(const ParallelizationPlan &ps,
                                   const PDG &pdg) {
  Loop *loop = pdg.getV().getLoop();
  const unsigned loopTime = perf->estimate_loop_weight(loop);
  const unsigned scaledLoopTime = Selector::FixedPoint * loopTime;
  const unsigned depthPenalty =
      Selector::PenalizeLoopNest *
      loop->getLoopDepth(); // break ties with nested loops
  unsigned adjLoopTime = scaledLoopTime;
  if (scaledLoopTime > depthPenalty)
    adjLoopTime = scaledLoopTime - depthPenalty;

  long estimatePipelineWeight =
      (long)Selector::FixedPoint * perf->estimate_pipeline_weight(ps, loop);
  const long wt = adjLoopTime - estimatePipelineWeight;
  long scaledwt = 0;

  if (perf->estimate_loop_weight(loop))
    scaledwt = wt * (double)lpl->getLoopTime(loop->getHeader()) /
               (double)perf->estimate_loop_weight(loop);

  return scaledwt;
}

PDG getExpectedPdg(const PDG &pdg, Criticisms &criticisms) {
  Vertices tmpV(pdg.getV().getLoop());
  PDG tmpPdg(pdg, tmpV, pdg.getControlSpeculator(), false /*ignoreAntiOutput*/);
  for (Criticism cr : criticisms) {
    Vertices::ID v, w;
    bool lc;
    DepType dt;
    std::tie(v, w, lc, dt) = cr;
    tmpPdg.removeEdge(v, w, lc, dt);
  }
  return tmpPdg;
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

        // check if this edge is removable
        if (int c = pdg.removableLoopCarriedEdge(i, dst)) {
          if (c == -1) {
            // criticism cannot be remedied. Abort
            DEBUG(errs() << "Cannot remove loop-carried edge(s) from "
                         << *pdg.getV().get(i) << " to " << *pdg.getV().get(dst)
                         << '\n');
            res.expSpeedup = -1;
            return res;
          }
        }

        complainAboutEdge(res, pdg, i, dst, true);
      }
    }
  }

  PDG expPdg = getExpectedPdg(pdg, res.criticisms);
  res.ps = getPipelineStrategy(expPdg);
  res.expSpeedup = getExpPipelineSpeedup(*res.ps, expPdg);

  return res;
}

} // namespace liberty
