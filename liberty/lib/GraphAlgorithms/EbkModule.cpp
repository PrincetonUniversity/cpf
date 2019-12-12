#include "liberty/GraphAlgorithms/EbkModule.h"
#include "llvm/Support/CommandLine.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/Utilities/ModuleLoops.h"

#include <assert.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#define DEBUG_TYPE "ebkmodule"

namespace liberty
{
namespace SpecPriv
{

using namespace llvm;

char EbkModule::ID = 0;
static RegisterPass<EbkModule> RP("ebk-module", "Run Extended Bron-Kerbosch algorithm to find maximum weighted clique", false, false);

cl::list<std::string> EbkWeights(
    "ebk-weights", cl::NotHidden, cl::CommaSeparated,
    cl::desc("List of loop weights: loop1,weight1,peeled1,loop2,wieght2,peeled2,..."));

cl::opt<std::string> EbkOutfile(
    "ebk-outfile", cl::init("ebk.out"), cl::NotHidden,
    cl::desc("Output file name"));

void EbkModule::getAnalysisUsage(AnalysisUsage& au) const
{
  au.addRequired< ModuleLoops >();
  au.addRequired< Targets >();
  au.setPreservesAll();
}

template <class T>
static T string_to(std::string s)
{
  T ret;
  std::stringstream ss(s);
  ss >> ret;

  if (!ss)
  {
    assert(false && "Failed to convert string to given type\n");
  }

  return ret;
}

bool EbkModule::runOnModule(Module& m)
{
  if ( EbkWeights.empty() )
  {
    errs() << "EbkModule input not given\n";
    return false;
  }

  assert( (EbkWeights.size() % 3) == 0 );

  for(unsigned i=0; i<EbkWeights.size(); i+=3)
  {
    std::string loopname = EbkWeights[i];
    unsigned    weight = string_to<unsigned>(EbkWeights[i+1]);
    weights[loopname] = weight;
    peeled[loopname] = (bool)(string_to<unsigned>(EbkWeights[i+2]));
  }

  Vertices vertices;
  computeVertices(vertices);

  Edges edges;
  computeEdges(vertices, edges);

  VertexWeights w;
  assignWeights(vertices, w);

  VertexSet maxClique;
  ebk(edges, w, maxClique);

  // print output

  std::ofstream of;
  of.open(EbkOutfile.c_str());

  for(VertexSet::iterator i=maxClique.begin(), e=maxClique.end(); i!=e; ++i)
  {
    const unsigned v = *i;

    Loop* A = vertices[v];
    BasicBlock* hA = A->getHeader();
    Function* fA = hA->getParent();
    std::string nA = fA->getName().str() + "::" + hA->getName().str();

    // print ones only included in ebk-weights list
    if ( !weights.count(nA) )
      continue;

    of << nA << " " << peeled[nA] << "\n";
  }

  of.close();

  errs() << "EbkModule Done!\n";

  return false;
}

////////////////////////////////////////////////////////////////////////////////
//
// computeVertices:
// put every LLVM Loop into vertices vector
//
////////////////////////////////////////////////////////////////////////////////

void EbkModule::computeVertices(Vertices& vertices)
{
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  const Targets &targets = getAnalysis< Targets >();
  for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i)
  {
    Loop* A = *i;
    BasicBlock* hA = A->getHeader();
    Function* fA = hA->getParent();
    std::string nA = fA->getName().str() + "::" + hA->getName().str();

    if ( weights.count(nA) )
      vertices.push_back(A);
  }
}

////////////////////////////////////////////////////////////////////////////////
//
// computeEdges:
// compute edges representing compatibility between loops
//
////////////////////////////////////////////////////////////////////////////////

static bool mustBeSimultaneouslyActive(const Loop *A, const Loop *B)
{
  return A->contains( B->getHeader() )
  ||     B->contains( A->getHeader() );
}

void EbkModule::computeEdges(const Vertices& vertices, Edges& edges)
{
  const unsigned N = vertices.size();
  for(unsigned i=0; i<N; ++i)
  {
    Loop *A = vertices[i];

    BasicBlock *hA = A->getHeader();
    Function *fA = hA->getParent();
    const Twine nA = fA->getName() + " :: " + hA->getName();

    for(unsigned j=i+1; j<N; ++j)
    {
      Loop *B = vertices[j];

      BasicBlock *hB = B->getHeader();
      Function *fB = hB->getParent();
      const Twine nB = fB->getName() + " :: " + hB->getName();

      /* If we can prove simultaneous activation,
       * exclude one of the loops */
      if( mustBeSimultaneouslyActive(A, B) )
      {
        LLVM_LLVM_DEBUG(errs() << "Loop " << nA << " is incompatible with loop "
                     << nB << " because of simultaneous activation.\n");
        continue;
      }

      LLVM_LLVM_DEBUG(errs() << "Loop " << nA << " is COMPATIBLE with loop "
                   << nB << ".\n");
      edges.insert( Edge(i,j) );
      edges.insert( Edge(j,i) );
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
//
// assignWeights:
// build VertexWeights data structure
//
////////////////////////////////////////////////////////////////////////////////

void EbkModule::assignWeights(Vertices& vertices, VertexWeights& w)
{
  const unsigned N = vertices.size();
  w.resize(N);

  for(unsigned i=0; i<N; ++i)
  {
    Loop* A = vertices[i];
    BasicBlock* hA = A->getHeader();
    Function* fA = hA->getParent();
    std::string nA = fA->getName().str() + "::" + hA->getName().str();

    assert( weights.count(nA) );
    w[i] = weights[nA];
  }
}

}
}
