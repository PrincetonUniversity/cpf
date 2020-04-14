#include "liberty/Talkdown/Node.h"
#include <iostream>
#include <map>
#include <unordered_map>

#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "talkdown"

using namespace llvm;

namespace llvm
{

  SESENode::SESENode()
  {
    is_leaf = false;
    depth = -1;

    parent = nullptr;
    basic_block = nullptr;

    annotations = std::unordered_map<std::string, std::string>();
    inherited_annotations = std::unordered_map<std::string, std::string>();
  }

  SESENode::SESENode(BasicBlock *bb) : SESENode()
  {
    basic_block = bb;
    is_leaf = true;

    // do we want to do this here or split it as a separate function?
    for ( auto &inst : *bb )
    {
      LLVM_DEBUG(
        llvm::errs() << "Inserting inst (" << *&inst << ") to ";
        if ( bb->hasName() )
          llvm::errs() << bb->getName().str() << "\n";
        else
        {
          bb->printAsOperand( llvm::errs(), false, bb->getParent()->getParent() );
          llvm::errs() << "\n";
        }
      );
      instructions.insert( &inst );
    }
  }

  void SESENode::setParent(SESENode *node)
  {
    parent = node;

    // correct place to do this?
    if ( node != nullptr)
      inherited_annotations = parent->getAnnotation();
    // annotations.insert(inherited_annotations.begin(), inherited_annotations.end());
  }

  void SESENode::addChild(SESENode *node)
  {
    this->is_leaf = false;
    children.push_back( node );
  }

  std::vector<SESENode *> SESENode::getChildren()
  {
    return this->children;
  }

  void SESENode::addInstruction(Instruction *i)
  {
    instructions.insert( i );
  }

  void SESENode::clearInstructions()
  {
    this->instructions.clear();
  }

  void SESENode::addAnnotation(std::pair<std::string, std::string> annot)
  {
    annotations.emplace( annot );
  }

  void SESENode::addAnnotations(std::unordered_map<std::string, std::string> annot)
  {
    annotations.insert( annot.begin(), annot.end() );
  }

  std::set<Instruction *> SESENode::getInstructions()
  {
    return this->instructions;
  }

  const std::unordered_map<std::string, std::string> &SESENode::getAnnotation() const
  {
    return annotations;
  }

  bool SESENode::isLeaf() const
  {
    return is_leaf;
  }

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

  // std::ostream &SESENode::recursivePrint(std::ostream &os) const
  std::ostream &SESENode::recursivePrint(std::ostream &os) const
  {
    os << this << "\n";
    if ( !is_leaf )
    {
      for ( auto child : children)
        child->recursivePrint( os );
    }

    return os;
    // return;
  }

  std::ostream &operator<<(std::ostream &os, const SESENode *node)
  {
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

    return os;
  }

  bool SESENode::basicBlockSameMetadata(BasicBlock *bb)
  {
    MDNode *meta;

    for ( auto &i : *bb )
    {
      // meta;
    }
  }

} // namespace llvm
