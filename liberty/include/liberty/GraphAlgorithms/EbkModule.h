#ifndef LLVM_LIBERTY_SPEC_PRIV_EBK_MODULE_H
#define LLVM_LIBERTY_SPEC_PRIV_EBK_MODULE_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "liberty/GraphAlgorithms/Ebk.h"
#include "liberty/GraphAlgorithms/Graphs.h"

#include <map>
#include <vector>

namespace liberty
{
namespace SpecPriv
{

using namespace llvm;

class EbkModule : public ModulePass
{
public:
  static char ID;
  EbkModule() : ModulePass(ID) {}
  ~EbkModule() {}

  void getAnalysisUsage(AnalysisUsage& au) const;

  bool runOnModule(Module& m);

private:
  typedef std::vector<Loop*> Vertices;

  void computeVertices(Vertices& vertices);
  void computeEdges(const Vertices& vertices, Edges& edges);
  void assignWeights(Vertices& vertices, VertexWeights& weights);

  std::map<std::string, unsigned> weights;
  std::map<std::string, bool> peeled;
};

}
}

#endif
