#pragma once

#include "llvm/IR/Instructions.h"
#include "llvm/IR/BasicBlock.h"

#include "llvm/Analysis/LoopInfo.h"

#include <set>
#include <vector>
#include <unordered_set>

#include "Annotation.h"

namespace AutoMP {

  struct Node
  {
  public:
    // constructors and destructors
    Node() : Node(nullptr, nullptr, nullptr) {}
		Node( Node *p, llvm::Loop *l = nullptr, llvm::BasicBlock *bb = nullptr ) :
			parent(p), loop(l), basic_block(bb) {}
		~Node();

    // Linking nodes together
    Node *getParent(void) { return parent; }
    void setParent(Node *p) { parent = p; }
    void addChild(Node *p) { children.emplace(p); }
    void removeChild(Node *p) { children.erase(p); }
    const std::set<Node *> &getChildren(void) const { return children; }

    // Getting and setting data of nodes
    void setID(int i) { ID = i; }
    int getID(void) const { return ID; }
    void setLoop(llvm::Loop *l) { loop = l; }
    llvm::Loop *getLoop(void) const { return loop; }
    void setBB(llvm::BasicBlock *bb) { basic_block = bb; }
    llvm::BasicBlock *getBB(void) const { return basic_block; }

    // Dealing with annotations
    bool containsAnnotationWithKey(std::string s) const;
    bool containsAnnotation(const Annotation &a) const;

    // printing stuff
    llvm::raw_ostream &recursivePrint(llvm::raw_ostream &) const;
    friend std::ostream &operator<<(std::ostream &, const Node *);
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &, const Node *);

    // TODO: make this private
    std::set<Annotation> annotations; // should this be a set? most likely

		static unsigned int how_many;

  private:
    int ID; // XXX this can be a static int so that it is easy to assign an ID to a node
    Node *parent;
    std::set<Node *> children;
    llvm::Loop *loop;
    llvm::BasicBlock *basic_block;
  };

  // XXX To be used later
  struct LoopContainerNode : public Node
  {
  private:
    // basic blocks contained within this loop (including subloops)
    // used to speed up finding basic blocks?
    std::unordered_set<llvm::BasicBlock *> contained_bbs;
  };

  // XXX Not used currently
  // All children of a BasicBlockContainerNode have the same annotation and belong
  // to the same loop.
  // It does however add some unnecessary nodes so maybe not use this...
  struct BasicBlockContainerNode : public Node
  {
  public:
    BasicBlockContainerNode()
    {
      assert(0 && "BasicBlockContainerNode not implemented yet");
    }
  private:
  };
} // namespace llvm
