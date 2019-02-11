#include "Ebk.h"

#include <algorithm>

namespace liberty
{
namespace SpecPriv
{

static void ebk(VertexSet &c, VertexSet &p, VertexSet &s, const Edges &E, const VertexWeights &W, VertexSet &best, int &bestWt)

{
  const unsigned N = W.size();

  if( p.empty() && s.empty() )
  {
    int c_wt = 0;
    for(unsigned i=0; i<c.size(); ++i)
      c_wt += W[ c[i] ];

    if( c_wt > bestWt )
    {
      bestWt = c_wt;
      best = c;
    }
    return;
  }

  while( !p.empty() )
  {
    const unsigned v = p.back();

    VertexSet p_intersect_nv, s_intersect_nv;
    for(unsigned i=0; i<N; ++i)
      if( E.count( Edge(v,i) ) )
      {
        // i in n(v)
        if( std::find(p.begin(), p.end(), i ) != p.end() )
          p_intersect_nv.push_back(i);
        if( std::find(s.begin(), s.end(), i ) != s.end() )
          s_intersect_nv.push_back(i);
      }

    c.push_back(v); // c = c U {v}
    ebk(c, p_intersect_nv, s_intersect_nv, E,W, best,bestWt);
    c.pop_back(); // restore c.

    // p = p \ {v}
    p.pop_back();
    // s = s U {v}
    s.push_back(v);
  }
}

int ebk(const Edges &E, const VertexWeights &W, VertexSet &mwcp)
{
  VertexSet c, p, s;

  // Initialize p := V
  const unsigned N = W.size();
  for(unsigned i=0; i<N; ++i)
    p.push_back(i);

  int bestWt = -1000;
  ebk(c,p,s, E,W, mwcp,bestWt);
  return bestWt;
}



}
}
