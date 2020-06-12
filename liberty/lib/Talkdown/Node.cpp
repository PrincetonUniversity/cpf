#define DEBUG_TYPE "talkdown"

#include "liberty/Talkdown/Node.h"

#include "llvm/Support/Debug.h"

#include <iostream>
#include <map>
#include <unordered_map>

using namespace llvm;

namespace AutoMP
{
  unsigned int Node::how_many = 0;

  Node::~Node()
  {
    how_many--;
  }

  bool Node::containsAnnotationWithKey(std::string s) const
  {
    for ( auto &a : annotations )
      if ( !a.get_key().compare( s ) )
        return true;
    return false;
  }

  std::ostream &Node::recursivePrint(std::ostream &os) const
  {
    os << this << "\n";
    if ( children.size() != 0 )
    {
      for ( auto child : children)
        child->recursivePrint( os );
    }

    return os;
    // return;
  }

  std::ostream &operator<<(std::ostream &os, const Node *node)
  {
    if ( !node->parent )
      /* os << "\033[1;31m** Root node **\033[0m\n"; */
      os << "** Root node **\n";
    else if ( node->children.size() == 0 )
      os << "** Leaf node **\n";
    else
      os << "** Intermediate node **\n";

    os << "\tID: " << node->ID << "\n";
    if ( node->parent )
      os << "\tParent ID: " << node->parent->ID << "\n";
    os << "\tAnnotations:\n";
    for ( auto &annot : node->annotations )
    {
      errs() << "\t\t" << annot.get_key() << " : " << annot.get_value() << "\n";
    }
    errs() << "\n";

#if 0
    if ( !node->is_leaf )
    {
      if ( !node->parent )
        os << "\033[1;31m** Root node **\033[0m\n";
      else
        os << "\033[36m-- Intermediate node --\033[0m\n";
    }
    else
    {
      os << "\033[32m++ Leaf node ++\033[0m\n";
      os << "\tFirst instruction:\n";
      assert(node->instructions.size() > 0);
      llvm::errs() << "\t\t" << *(node->instructions.begin()) << "\n";
    }

    os << "\tAnnotations:\n";
    os << "\t\tInherited:\n";
    for ( auto &annot : node->inherited_annotations )
      os << "\t\t\t" << annot.first << " : " << annot.second << "\n";

    os << "\t\tUninherited:\n";
    for ( auto &annot : node->annotations )
      os << "\t\t\t" << annot.first << " : " << annot.second << "\n";

#endif
    return os;
  }

} // namespace llvm
