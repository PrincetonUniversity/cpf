#pragma once

#include "llvm/IR/Function.h"

#include "Node.h"

#include <string>

using namespace llvm;

namespace llvm {

  class FunctionTree
  {
    public:
      FunctionTree();
      FunctionTree(Function *f);
      ~FunctionTree();
      bool constructTree(Function *f);

      SESENode *getInnermostNode(Instruction *);
      SESENode *getParent(SESENode *);
      SESENode *getFirstCommonAncestor(SESENode *, SESENode *);

      friend std::ostream &operator<<(std::ostream &, const FunctionTree &);

      void print();
      void writeDotFile( const std::string filename );

    private:

      /*
       * Associated function
       */
      Function *associated_function;

      /*
       * Root node of tree
       */
      SESENode *root;

      /*
       * Split nodes of tree recursively
       */
      bool splitNodesRecursive(SESENode *node);

      /*
       * NOTE(gc14): Maps instructions to its appropriate SESENode
       */
      std::unordered_map<Instruction *, SESENode *> inst_node_map;

#if 0
      /*
       * Split basic block when annotation changes
       */
      bool insertSplits();
#endif
  };

} // namespace llvm
