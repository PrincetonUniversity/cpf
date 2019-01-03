#ifndef LLVM_LIBERTY_SPEC_PRIV_EBK_H
#define LLVM_LIBERTY_SPEC_PRIV_EBK_H

#include <vector>
#include <set>

#include "liberty/SpecPriv/Graphs.h"

namespace liberty
{
namespace SpecPriv
{
  // Extended Bron-Kerbosch http://arxiv.org/abs/1101.1266
  // An algorithm to find exact solutions to the
  // Maximum Weighted Clique Problem (MWCP).
  int ebk(const Edges &E, const VertexWeights &W, VertexSet &mwcp);

}
}

#endif

