#define DEBUG_TYPE "talkdown"

#include "liberty/Talkdown/Node.h"

#include "llvm/Support/Debug.h"

#include <iostream>
#include <map>
#include <unordered_map>

using namespace llvm;

namespace AutoMP
{

#if 0
  void SESENode::addAnnotationsFromBasicBlock()
  {
    Annotations annotations_to_add = {};
    MDNode *meta;
    for ( auto &inst : *basic_block )
    {
      /* llvm::errs() << "-- " << inst << " --\n"; */
      meta = inst.getMetadata("note.noelle");
      if ( meta != nullptr )
      {
        for ( auto &pair_operand : meta->operands() )
        {
          /* llvm::errs() << "\t"; */
          auto *pair = dyn_cast<MDNode>(pair_operand.get());
          pair->print( llvm::errs(), basic_block->getModule() );
          auto *key = dyn_cast<MDString>(pair->getOperand(0));
          auto *value = dyn_cast<MDString>(pair->getOperand(1));
          // annotations_to_add.emplace( key, value );
          /* llvm::errs() << " --> " << key->getString().str() << " : " << value->getString().str() << "\n"; */
        }
      }
    }
  }

  std::unordered_map<std::string, std::string> parse_metadata(MDNode *meta_node)
  {
    std::unordered_map<std::string, std::string> annotation_result = {};
    for ( auto &pair_operand : meta_node->operands() )
    {
      auto *pair   = dyn_cast<MDNode>(pair_operand.get());
      auto *key    = dyn_cast<MDString>(pair->getOperand(0));
      auto *value  = dyn_cast<MDString>(pair->getOperand(1));
      annotation_result.emplace(key->getString().str(), value->getString().str());
      std::cerr << "\t" << key->getString().str() << " : " << value->getString().str() << "\n";
    }

    return annotation_result;
  }

  const std::vector<std::pair<Instruction *, SESENode::Annotations> > SESENode::findSplitPoints() const
  {
    MDNode *last_meta_node = nullptr;
    bool has_meta = false;
    std::vector<std::pair<Instruction *, Annotations> > split_complete;
    for ( auto &inst : *basic_block )
    {
      has_meta = inst.hasMetadata();

      // if metadata changed between instructions, split basic block
      if ( (!has_meta && last_meta_node != nullptr ) ||
           (inst.getMetadata("note.noelle") != last_meta_node) )
      {
        // not first or last instruction in basic block
        if ( &inst != &*basic_block->begin() && &inst != &*std::prev(basic_block->end()) )
        {
          std::unordered_map<std::string, std::string> current_annot = parse_metadata( inst.getMetadata("note.noelle") );
          llvm::errs() << "Splitting block " << basic_block->getName() << " at instruction " << inst << "\n";
          std::cerr << "Adding these noelle notes to split:\n";
          /* for ( auto it : current_annot ) */
          /*   std::cerr << "\t" << it.first << " : " << it.second << "\n"; */
          split_complete.push_back( std::pair<Instruction *, Annotations>(&inst, current_annot) );
        }

        if ( has_meta )
        {
          last_meta_node = inst.getMetadata("note.noelle");
          // last_meta_node->dump();
        }
      }
    }

    // TODO(greg): add annotations to split points
    return split_complete;
  }

#endif
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
