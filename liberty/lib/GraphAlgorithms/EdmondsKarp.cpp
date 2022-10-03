#define DEBUG_TYPE "edmonds-karp"

#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <list>

#include "liberty/GraphAlgorithms/EdmondsKarp.h"

namespace liberty::SpecPriv {
using namespace llvm;

// It was ~0U here, but EdgeWegiht is unsigned long, leading to a bug when weight
// is more than unsigned (>~0U)
const EdgeWeight Infinity(~0UL);

const Vertex Source(0);
const Vertex Sink(1);

static EdgeWeight bfsFindAugmentingPath(Adjacencies &adj, EdgeWeights &cap,
                                        EdgeWeights &used,
                                        std::map<Vertex, Vertex> &preds) {

  preds.clear();
  preds[Source] = Source;

  // Build reverse adjacency list -- we need this because the residual graph
  // contains reverse edges from the original graph.
  Adjacencies revAdj;
  for (auto &it : adj) {
    for (auto w = it.second.begin(), we = it.second.end(); w != we; ++w) {
      revAdj[*w].push_back(it.first);
    }
  }

  // It was <Vertex, double> here, when Infinity corrected to ~0UL,
  // the later conversion is narrorwed, (unsigned long)(double)(~0UL) = 0
  using VertexAndFlow = std::pair<Vertex, EdgeWeight>;
  using Fringe = std::list<VertexAndFlow>;
  Fringe fringe;
  fringe.push_back(VertexAndFlow(Source, Infinity));
  while (!fringe.empty()) {
    VertexAndFlow &vnf = fringe.front();
    Vertex v = vnf.first;
    EdgeWeight flow = vnf.second;

    fringe.pop_front();

    for (auto i = adj[v].begin(), e = adj[v].end(); i != e; ++i) {
      Vertex w = *i;

      EdgeWeight totalCapacity = 0, usedCapacity = 0;
      if (cap.count(Edge(v, w)))
        totalCapacity = cap[Edge(v, w)];
      if (used.count(Edge(v, w)))
        usedCapacity = used[Edge(v, w)];

      EdgeWeight rem = 0;
      if (totalCapacity == Infinity)
        rem = Infinity;
      else
        rem = totalCapacity - usedCapacity;

      if (rem > 0 && !preds.count(w)) {
        preds[w] = v;
        EdgeWeight min = std::min(flow, rem);

        if (w == Sink)
          return min;
        else
          fringe.push_back(VertexAndFlow(w, min));
      }
    }

    // Also for reverse edges
    for (auto i = revAdj[v].begin(), e = revAdj[v].end(); i != e; ++i) {
      Vertex u = *i;

      EdgeWeight rem = 0;
      if (used.count(Edge(u, v)))
        rem = used[Edge(u, v)];

      if (rem > 0 && !preds.count(u)) {
        preds[u] = v;
        EdgeWeight min = std::min(flow, rem);
        fringe.push_back(VertexAndFlow(u, min));
      }
    }
  }

  return 0;
}

static EdgeWeight maxFlowEdmondsKarp(Adjacencies &adj, EdgeWeights &cap,
                                     EdgeWeights &flow) {
  EdgeWeight total = 0;

  flow.clear();
  std::map<Vertex, Vertex> parents;

  for (;;) {
    EdgeWeight additional = bfsFindAugmentingPath(adj, cap, flow, parents);

    if (additional == 0)
      break;

    Vertex w = Sink;
    while (w != Source) {
      Vertex u = parents[w];

      if (!cap.count(Edge(u, w)))
        flow[Edge(u, w)] = 0;
      if (!cap.count(Edge(w, u)))
        flow[Edge(w, u)] = 0;

      flow[Edge(u, w)] += additional;
      flow[Edge(w, u)] -= additional;

      w = u;
    }

    total += additional;
  }

  LLVM_DEBUG(errs() << "\t\t- Edmonds-Karp: found a max-flow of " << total
                    << ".\n");
  return total;
}

void computeMinCut(Adjacencies &adj, EdgeWeights &cap, VertexSet &minCut) {

  LLVM_DEBUG(errs() << "\t- Starting max-flow...\n");
  EdgeWeights flow;
  EdgeWeight maxFlow = maxFlowEdmondsKarp(adj, cap, flow);
  LLVM_DEBUG(errs() << "\t- Max-flow is done; max_flow=" << maxFlow << ".\n");

  LLVM_DEBUG(errs() << "\t- Computing min-cut as those nodes reachable from "
                       "root in residual graph.\n");

  std::set<Vertex> reachable;
  reachable.insert(Source);

  // Build reverse adjacency list -- we need this because the residual graph
  // contains reverse edges from the original graph.
  Adjacencies revAdj;
  for (auto &it : adj) {
    for (auto w = it.second.begin(), we = it.second.end(); w != we; ++w) {
      revAdj[*w].push_back(it.first);
    }
  }

  std::list<Vertex> fringe;
  fringe.push_back(Source);
  while (!fringe.empty()) {
    Vertex v = fringe.front();
    fringe.pop_front();

    for (auto i = adj[v].begin(), e = adj[v].end(); i != e; ++i) {
      Vertex w = *i;

      EdgeWeight totalCap = cap[Edge(v, w)];
      EdgeWeight used = flow[Edge(v, w)];
      EdgeWeight residual = 0;
      if (totalCap == Infinity)
        residual = Infinity;
      else
        residual = totalCap - used;

      if (!reachable.count(w) && residual > 0) {
        reachable.insert(w);
        fringe.push_back(w);
      }
    }

    // We also need to explore reverse edges, as these exist in the residual
    // graph.
    for (auto i = revAdj[v].begin(), e = revAdj[v].end(); i != e; ++i) {
      Vertex u = *i;

      EdgeWeight residual = flow[Edge(u, v)];

      if (!reachable.count(u) && residual > 0) {
        reachable.insert(u);
        fringe.push_back(u);
      }
    }
  }

  minCut.clear();

  // Now figure out the min-cut from the reachable set
  for (auto &i : adj) {
    Vertex v = i.first;
    for (unsigned int w : i.second) {
      if ((reachable.count(v) && !reachable.count(w)) ||
          (!reachable.count(v) && reachable.count(w))) {

        // this edge v->w is cut.
        // either v is an SCC, or w is an SCC.
        minCut.push_back(v);
        minCut.push_back(w);
      }
    }
  }

  LLVM_DEBUG(errs() << "\t- Done computing min-cut.\n");
}

} // namespace liberty::SpecPriv
