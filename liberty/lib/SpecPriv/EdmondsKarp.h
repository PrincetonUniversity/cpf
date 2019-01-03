#ifndef LLVM_LIBERTY_SPEC_PRIV_EDMONDS_KARP_H
#define LLVM_LIBERTY_SPEC_PRIV_EDMONDS_KARP_H

#include <vector>
#include <set>

#include "liberty/SpecPriv/Graphs.h"

namespace liberty
{
namespace SpecPriv
{

Vertex leftVertex(unsigned num);
Vertex rightVertex(unsigned num);

void computeMinCut(
  Adjacencies &adj,
  EdgeWeights &cap,
  VertexSet &minCut);

}
}
#endif
