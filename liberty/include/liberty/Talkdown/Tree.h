#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Analysis/LoopInfo.h"

#include "liberty/Utilities/ModuleLoops.h"
#include "Node.h"
#include "Annotation.h"

#include <string>

namespace AutoMP {


  class FunctionTree
  {
  public:
    // constructors
    FunctionTree() : root(nullptr), num_nodes(0) {}
    FunctionTree(llvm::Function *f);
    ~FunctionTree();

    bool constructTree(llvm::Function *f, llvm::LoopInfo &li); // should use this instead
    bool constructTreeFromNode(Node *n); // use subtrees later for something???

    // getters and setters
    llvm::Function *getFunction(void) const { return associated_function; }

    // printing stuff
    void printNodeToInstructionMap(void) const;
    friend std::ostream &operator<<(std::ostream &, const FunctionTree &);
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &, const FunctionTree &);

    const AnnotationSet &getAnnotationsForInst(const llvm::Instruction *) const;
    const AnnotationSet &getAnnotationsForInst(const llvm::Instruction *, const llvm::Loop *) const;

    void writeDotFile(const std::string filename);

    std::vector<Node *> nodes; // remove this once an iterator is developed

  private:
    llvm::Function *associated_function;
    Node *root;

    int num_nodes;

    const Node *findNodeForLoop(const Node *, const llvm::Loop *) const; // level-order traversal
    const Node *findNodeForBasicBlock(const Node *, const llvm::BasicBlock *) const;
    const Node *findNodeForInstruction(const Node *, const llvm::Instruction *) const;
    Node *searchUpForAnnotation(Node *start, std::pair<std::string, std::string> a) __attribute__ ((deprecated)); // search upward from a node to find first node with matching annotation
    std::vector<Node *> getNodesInPreorder(Node *start) const;
    std::vector<LoopContainerNode *> getAllLoopContainerNodes(void) const;
    std::vector<Node *> getAllLoopBasicBlockNodes(void) const;

    void addLoopContainersToTree(llvm::LoopInfo &li);
    void annotateBasicBlocks(void);
    void annotateLoops(void);

    // Change these to use the node instead so we don't have to traverse the tree?
    void addBasicBlocksToLoops(llvm::LoopInfo &li);
    void addNonLoopBasicBlocks(llvm::LoopInfo &li);

    // If something like a "critical" pragma is attached to a basic block, do something
    void backAnnotateLoopFromBasicBlocks(llvm::Loop *l);

    bool splitBasicBlocksByAnnotation(void);
    bool fixBasicBlockAnnotations(void);

    bool handleCriticalAnnotations(void);
    bool handleOwnedAnnotations(void);

  public:
    // XXX: First attempt at creating custom iterator. Maybe at a const_iterator later?
    // I have no idea what I'm doing lol
    class iterator
    {
      Node *start;
      iterator(Node *n) : start(n) {}
      /* iterator &operator++() {} */
    };


  };

  // Things to consider:
  //  - how to implement an efficient iterator? If we want to traverse the tree often, then we should
  //    use a std::set and add to it every time a node is added (probably worth it). Otherwise, manually
  //    do a level-order traversal every time????

} // namespace AutoMP
