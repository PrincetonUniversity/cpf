#ifndef LIBERTY_TYPEDEFS_H
#define LIBERTY_TYPEDEFS_H

#include <vector>
#include <list>

#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"

namespace liberty {

  using namespace llvm;

  class SCCNode;

  typedef unsigned                              ThreadNo;

  typedef std::vector<BasicBlock*>              BBList;

  typedef DenseSet<BasicBlock *>                BBSet;
  typedef std::vector<BBSet>                    BBSets;

  typedef DenseSet<Function *>                  FuncSet;

  typedef std::pair<TerminatorInst *, unsigned> OutEdge;
  typedef DenseSet<OutEdge>                     OutEdgeSet;
  typedef OutEdgeSet::const_iterator            OESI;
  typedef std::vector<OutEdge>                  OutEdgeList;

  typedef DenseMap<BasicBlock*, unsigned int>   bbTripCountMap_t;

  typedef DenseSet<llvm::Value *>               ValueSet;
  typedef std::vector<ValueSet>                 ValueSets;

  typedef ValueSet::const_iterator              VI;
  typedef BBSet::const_iterator                 BI;
  typedef std::vector<Value*>                   ValueList;
  typedef std::vector<std::vector<Value*> >     ValueLists;
  typedef std::vector<Value*>::const_iterator   VLI;


  typedef DenseSet<SCCNode*> SCCSet;
  typedef std::list<SCCNode *> SCCList;

  // A trivial, STL-container based
  // graph, used for the NM-flow network
  /*
  typedef std::pair<unsigned,SCCNode*> Vertex;
  typedef std::pair<Vertex,Vertex> Edge;
  typedef std::vector<Vertex> VertexList;
  typedef DenseMap<Vertex,VertexList> Adjacencies;
  typedef double Weight;
  typedef DenseMap<Edge, Weight> Weights;
  */


}


#endif

