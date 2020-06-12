#pragma once

#include "llvm/IR/Instructions.h"
#include "llvm/IR/BasicBlock.h"

#include "llvm/Analysis/LoopInfo.h"

#include <set>
#include <vector>

#include "Annotation.h"

using namespace llvm;

namespace AutoMP {

	/// Node class for representing SESE regions or loops or a function (only root node).
	/// Note that nodes not attached to a tree are not allowed, since how_many will be screwed up
  struct Node
  {
  public:
    Node() : Node(nullptr, nullptr, nullptr) {}
		Node( Node *p, Loop *l = nullptr, BasicBlock *bb = nullptr, std::vector<Annotation> v = std::vector<Annotation>() ) :
			parent(p), loop(l), basic_block(bb) {}
		~Node();

    Node *getParent(void) { return parent; }
    void setParent(Node *p) { parent = p; }
    void addChild(Node *p) { children.emplace(p); }
    void removeChild(Node *p) { children.erase(p); }
    const std::set<Node *> &getChildren(void) { return children; }

    void setID(int i) { ID = i; }
    int getID(void) { return ID; }
    void setLoop(Loop *l) { loop = l; }
    Loop *getLoop(void) { return loop; }
    void setBB(BasicBlock *bb) { basic_block = bb; }
    BasicBlock *getBB(void) { return basic_block; }

    bool containsAnnotationWithKey(std::string s) const;
    bool containsAnnotation(const Annotation &a) const;

    std::ostream &recursivePrint(std::ostream &) const;
    friend std::ostream &operator<<(std::ostream &, const Node *);

    // TODO: make this private
    std::set<Annotation> annotations; // should this be a set? most likely

		static unsigned int how_many;

  private:
    int ID;
    Node *parent;
    std::set<Node *> children;
    Loop *loop; // XXX: Duplicated from annotation for now
    BasicBlock *basic_block;
  };
} // namespace llvm
