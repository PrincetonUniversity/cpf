#define DEBUG_TYPE "edmonds-karp"

#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <list>

#include "EdmondsKarp.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

const EdgeWeight Infinity(~0U);

const Vertex Source(0);
const Vertex Sink(1);

static EdgeWeight bfsFindAugmentingPath(
  Adjacencies &adj,
  EdgeWeights &cap,
  EdgeWeights &used,
  std::map<Vertex,Vertex> &preds)
{

  preds.clear();
  preds[Source] = Source;

  // Build reverse adjacency list -- we need this because the residual graph
  // contains reverse edges from the original graph.
  Adjacencies revAdj;
  for (Adjacencies::iterator it = adj.begin(), ie = adj.end(); it != ie; ++it) {
    for (VertexSet::iterator w = (*it).second.begin(), we = (*it).second.end();
         w != we; ++w) {
      revAdj[*w].push_back((*it).first);
    }
  }

  typedef std::pair<Vertex,double> VertexAndFlow;
  typedef std::list<VertexAndFlow> Fringe;
  Fringe fringe;
  fringe.push_back( VertexAndFlow(Source, Infinity) );
  while( ! fringe.empty() ) {
    VertexAndFlow &vnf = fringe.front();
    Vertex v = vnf.first;
    EdgeWeight flow = vnf.second;
    fringe.pop_front();

    for(VertexSet::iterator i=adj[v].begin(), e=adj[v].end(); i!=e; ++i) {
      Vertex w = *i;

      EdgeWeight totalCapacity = 0, usedCapacity = 0;
      if( cap.count( Edge(v,w) ) )
        totalCapacity = cap[ Edge(v,w) ];
      if( used.count( Edge(v,w) ) )
        usedCapacity = used[ Edge(v,w) ];

      EdgeWeight rem = 0;
      if( totalCapacity == Infinity )
        rem = Infinity;
      else
        rem = totalCapacity - usedCapacity;

      if( rem>0 && !preds.count(w) ) {
        preds[w] = v;
        EdgeWeight min = std::min(flow, rem);

        if( w == Sink )
          return min;
        else
          fringe.push_back( VertexAndFlow(w, min) );
      }
    }

    // Also for reverse edges
    for(VertexSet::iterator i=revAdj[v].begin(), e=revAdj[v].end(); i!=e; ++i) {
      Vertex u = *i;

      EdgeWeight rem = 0;
      if( used.count( Edge(u,v) ) )
        rem = used[ Edge(u,v) ];

      if( rem>0 && !preds.count(u) ) {
        preds[u] = v;
        EdgeWeight min = std::min(flow, rem);
        fringe.push_back( VertexAndFlow(u, min) );
      }
    }
  }

  return 0;
}


static EdgeWeight maxFlowEdmondsKarp(
  Adjacencies &adj,
  EdgeWeights &cap,
  EdgeWeights &flow)
{
  EdgeWeight total = 0;

  flow.clear();
  std::map<Vertex,Vertex> parents;

  for(;;) {
    EdgeWeight additional = bfsFindAugmentingPath(adj,cap,flow,parents);

    if( additional == 0 )
      break;

    Vertex w = Sink;
    while( w != Source ) {
      Vertex u = parents[w];

      if( ! cap.count( Edge(u,w) ) )
        flow[ Edge(u,w) ] = 0;
      if( ! cap.count( Edge(w,u) ) )
        flow[ Edge(w,u) ] = 0;

      flow[ Edge(u,w) ] += additional;
      flow[ Edge(w,u) ] -= additional;

      w = u;
    }

    total += additional;
  }

  DEBUG(errs() << "\t\t- Edmonds-Karp: found a max-flow of " << total << ".\n");
  return total;
}

void computeMinCut(
  Adjacencies &adj,
  EdgeWeights &cap,
  VertexSet &minCut)
{

  DEBUG(errs() << "\t- Starting max-flow...\n");
  EdgeWeights flow;
  EdgeWeight maxFlow = maxFlowEdmondsKarp(adj,cap,flow);
  DEBUG(errs() << "\t- Max-flow is done; max_flow=" << maxFlow << ".\n");

  DEBUG(errs() << "\t- Computing min-cut as those nodes reachable from root in residual graph.\n");

  std::set<Vertex> reachable;
  reachable.insert(Source);

  // Build reverse adjacency list -- we need this because the residual graph
  // contains reverse edges from the original graph.
  Adjacencies revAdj;
  for (Adjacencies::iterator it = adj.begin(), ie = adj.end(); it != ie; ++it) {
    for (VertexSet::iterator w = (*it).second.begin(), we = (*it).second.end();
         w != we; ++w) {
      revAdj[*w].push_back((*it).first);
    }
  }

  std::list<Vertex> fringe;
  fringe.push_back( Source );
  while( !fringe.empty() ) {
    Vertex v = fringe.front();
    fringe.pop_front();

    for(VertexSet::iterator i=adj[v].begin(), e=adj[v].end(); i!=e; ++i) {
      Vertex w = *i;

      EdgeWeight totalCap = cap[ Edge(v,w) ];
      EdgeWeight used = flow[ Edge(v,w) ];
      EdgeWeight residual = 0;
      if( totalCap == Infinity )
        residual = Infinity;
      else
        residual = totalCap - used;

      if(!reachable.count(w) && residual>0 ) {
        reachable.insert( w );
        fringe.push_back( w );
      }
    }

    // We also need to explore reverse edges, as these exist in the residual graph.
    for (VertexSet::iterator i = revAdj[v].begin(), e = revAdj[v].end();
         i != e; ++i) {
      Vertex u = *i;

      EdgeWeight residual = flow[ Edge(u,v) ];

      if (!reachable.count(u) && residual>0 ) {
        reachable.insert( u );
        fringe.push_back( u );
      }
    }
  }

  minCut.clear();

  // Now figure out the min-cut from the reachable set
  for (Adjacencies::iterator i = adj.begin(), e = adj.end(); i != e; ++i) {
    Vertex v = (*i).first;
    for (VertexSet::iterator j = (*i).second.begin(), je = (*i).second.end();
         j != je; ++j) {
      Vertex w = *j;
      if ((reachable.count(v) && !reachable.count(w)) ||
          (!reachable.count(v) && reachable.count(w))) {

        // this edge v->w is cut.
        // either v is an SCC, or w is an SCC.
        minCut.push_back(v);
        minCut.push_back(w);
      }
    }
  }

  DEBUG(errs() << "\t- Done computing min-cut.\n");
}

}
}
