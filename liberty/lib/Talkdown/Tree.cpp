#include "llvm/IR/Metadata.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "liberty/Talkdown/Tree.h"

#include <iostream>

using namespace llvm;

namespace llvm
{

  FunctionTree::FunctionTree()
  {
    root = nullptr;
  }

  FunctionTree::FunctionTree(Function *f) : FunctionTree()
  {
    associated_function = f;
  }

  FunctionTree::~FunctionTree()
  {

  }

  SESENode *FunctionTree::getInnermostNode(Instruction *inst)
  {

  }

  SESENode *FunctionTree::getParent(SESENode *node)
  {
    return node->getParent();
  }

  SESENode *FunctionTree::getFirstCommonAncestor(SESENode *n1, SESENode *n2)
  {
    SESENode *deeper;
    SESENode *shallower;

    // if same node, return one of them
    if ( n1 == n2 )
      return n1;

    // find which node is deeper in the tree
    if ( n1->getDepth() > n2->getDepth() )
    {
      deeper = n1;
      shallower = n2;
    }
    else
    {
      deeper = n2;
      shallower = n1;
    }

    // get the two search nodes to the same depth
    while ( shallower->getDepth() != deeper->getDepth() )
      deeper = deeper->getParent();

    while ( deeper != root )
    {
      if ( deeper == shallower )
        return deeper;

      deeper = deeper->getParent();
      shallower = shallower->getParent();
    }

    // if we get to root, then there was no common ancestor
    return nullptr;
  }

  bool FunctionTree::splitNodesRecursive(SESENode *node)
  {
    for ( auto child : node->getChildren() )
    {
      auto split_points = child->findSplitPoints();

      if ( split_points.size() == 0 )
        continue;

      // TODO(greg): Split basic block and change tree
      //  1. Create nodes with instructions from before and after split points
      //  2. Remove instructions from current node
      //  3. Set children of current node to be nodes created in (1)
      auto current_inst = child->getInstructions().begin();
      for ( auto split : split_points )
      {
        SESENode *added_node = new SESENode();
        while ( *current_inst != split.first )
        {
          added_node->addInstruction( *current_inst );
          current_inst++;
        }
        added_node->setDepth( child->getDepth() + 1 );
        added_node->addAnnotations( split.second ); // XXX(greg): Adding to the wrong node (I think)
        child->addChild( added_node );
      }

      // need to add instructions after last split
      SESENode *added_node = new SESENode();
      while ( current_inst != child->getInstructions().end() )
      {
        added_node->addInstruction( *current_inst );
        current_inst++;
      }
      child->addChild( added_node );

      // clear instructions from intermediate node
      child->clearInstructions();

      // call the recursive function here??
      /* for ( auto childchild : child->getChildren() ) */
      /*   splitNodesRecursive( childchild ); */
    }

    return false;
  }

  bool FunctionTree::constructTree(Function *f)
  {
    bool modified = false;

    // TESTING
    for ( auto &bb : *f )
      for ( auto &i : bb )
      {
        errs() << *&i << ":\n\t\t";
        i.getDebugLoc().print(errs());
        errs() << "\n";

        i.getMetadata(0);
      }


    // construct root node
    // TODO(greg): with annotation of function if there is one
    this->root = new SESENode();
    root->setDepth( 0 );
    root->addAnnotation(std::pair<std::string, std::string>("Root", "Default"));
    root->setParent( nullptr );

    // add all basic blocks as children of root
    // add metadata that applies to all instructions of a basic block to each node
    for ( auto &bb : *associated_function )
    {
      SESENode *node = new SESENode( &bb );
      node->addAnnotationsFromBasicBlock();
      node->setParent( root );
      node->setDepth( 1 );
      root->addChild( node );
    }

    return false;


    root->clearInstructions();
    // TODO(greg): Need to add instructions to the hash map

    // split children nodes recursively until all annotations are split
    // splitNodesRecursive( root ); // XXX(greg): add this back

    return modified;
  }

  void FunctionTree::writeDotFile( const std::string filename )
  {
    // NOTE(greg): Use GraphWriter to print out the tree better
  }

  std::ostream &operator<<(std::ostream &os, const FunctionTree &tree)
  {
    os << "Function: " << tree.associated_function->getName().str() << "\n";
    return tree.root->recursivePrint( os );
  }

} // namespace llvm
