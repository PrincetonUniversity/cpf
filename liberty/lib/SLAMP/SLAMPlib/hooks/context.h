#ifndef SLAMPLIB_HOOKS_SLAMP_CONTEXT_H
#define SLAMPLIB_HOOKS_SLAMP_CONTEXT_H

/* The context management
 * ======================
 * The definition of a context is very specific to the client. For SpecPriv,
 * the dynamic context is defined as the function and loop stack.
 *
 * For loop-aware memory dependnece profiling, the context is the loop and the
 * invocation and iteration count.
 *
 * The context information usually needs to be stored in the shadow memory.
 * 
 * The context management is responsible for keeping track the current context
 * and creating a fixed length representation of any context.
 *
 */

#include <iostream>
#include <stack>
#include <utility>
#include <map>
#include "llvm/IR/Instruction.h"
#include "scaf/Utilities/Metadata.h"

namespace SLAMPLib {

using ContextHash = size_t;

/// The context is stack of the ContextId
template <typename ContextId> struct Context {
  ContextId id;
  Context *parent;

  Context(ContextId id, Context *parent) : id(std::move(id)), parent(parent) {}

  /// Add a new context to the stack
  Context *chain(ContextId id) { return new Context(id, this); }

  static Context* getTopContext() {
    return new Context(ContextId::getTopContextId(), nullptr);
  }

  ContextHash hash() {
    ContextHash h = id.hash();
    if (parent)
      h ^= parent->hash();
    return h;
  }

  /// Remove the top context from the stack
  Context *pop() {
    auto parent = this->parent;
    // FIXME: is this ok to use?
    delete this;
    return parent;
  }
  using FlattenContext = std::vector<ContextId>;
  using ContextIdType = ContextId;

  /// make the context into a vector of only the context id
  FlattenContext flatten() {
    std::vector<ContextId> result;
    Context *cur = this;
    while (cur) {
      result.push_back(cur->id);
      cur = cur->parent;
    }
    return result;
  };

  /// print context
  void print(std::ostream &os) {
    os << "Context:\n";
    Context *cur = this;
    while (cur) {
      cur->id.print(os);
      cur = cur->parent;
    }
  }
};

/// Note that we do no need to keep track of
/// all the objects that are alive
template <class Context>
struct ContextManager {
  Context *activeContext;

  ContextManager() {
    activeContext = Context::getTopContext();
  }

  std::unordered_map <ContextHash, typename Context::FlattenContext> contextMap;

  ContextHash activeId() {
    if (activeContext)
      return activeContext->hash();
    else
      return 0;
  }

  void updateContext(typename Context::ContextIdType contextId) {
    assert(activeContext && "active context is null");
    /// maybe this is not a great idea, expose the semantics of the context to the manager
    activeContext = activeContext->chain(contextId);
    // addContext(activeContext->hash(), *activeContext);
  }

  void popContext(typename Context::ContextIdType contextId) {
    assert(activeContext && "active context is null");

    if (activeContext->id == contextId) {
      activeContext = activeContext->pop();
    } else {
      // The context is not matching, could be due to the longjmp/setjmp etc or
      // exception handling in C++
      std::cerr << "Context mismatch! Current context: ";
      auto tmp = activeContext;
      while (tmp->parent) {
        std::cerr << "(" << tmp->id.type << "," << tmp->id.metaId << ")->";
        tmp = tmp->parent;
      }
      std::cerr << "(" << tmp->id.type << "," << tmp->id.metaId << ")->";
      std::cerr << "Exiting context: (" << contextId.type << ","
                << contextId.metaId << ")" << std::endl;

      // Let's try to find the correct context
      bool foundInStack = false;
      while (activeContext->parent) {
        activeContext = activeContext->pop();
        if (activeContext->id == contextId) {
          foundInStack = true;
          break;
        }
      }

      assert(foundInStack && "Could not find the exiting context in the stack");
    }
  }

  void addContext(ContextHash hash, Context &context) {

    // if exist one but not the same, then we have a problem
    if (contextMap.count(hash) && contextMap[hash] != context.flatten()) {
      // FIXME: need to turn this back on
      // assert(false && "Context hash collision");
    }

    // insert the context
    contextMap[hash] = context.flatten();
  }
};

namespace SpecPrivLib {
enum SpecPrivContextType {
  TopContext = 0,
  FunctionContext,
  LoopContext,
};


struct ContextId {
  SpecPrivContextType type;
  int32_t metaId;
  ContextId(SpecPrivContextType type, int32_t id) : type(type), metaId(id) {}
  ContextId(std::pair<SpecPrivContextType, int32_t> p)
      : type(p.first), metaId(p.second) {}
  static ContextId getTopContextId() { return {TopContext, 0}; }
  // id left shift 2 bits and or with type
  ContextHash hash() {
    return (static_cast<ContextHash>(metaId) << 2) | static_cast<ContextHash>(type);
  }

  bool operator==(const ContextId &other) const {
    return type == other.type && metaId == other.metaId;
  }

  bool operator<(const ContextId &other) const {
    return type < other.type || (type == other.type && metaId < other.metaId);
  }

  void print(std::ostream &os) {
    os << "ContextId: " << type << " " << metaId << "\n";
  }
};
using SpecPrivContext = Context<ContextId>;

struct SpecPrivContextManager : public ContextManager<SpecPrivContext> {

  std::map<std::vector<ContextId>, ContextHash> contextIdHashMap;
  size_t contextIdHashCounter = 1;

  ContextHash encodeContext(SpecPrivContext context) {
    std::vector<ContextId> flattenContext = context.flatten();
    if (contextIdHashMap.count(flattenContext)) {
      return contextIdHashMap[flattenContext];
    } else {
      contextIdHashMap[flattenContext] = contextIdHashCounter;
      contextMap[contextIdHashCounter] = flattenContext;

      return contextIdHashCounter++;
    }
  }

  ContextHash encodeActiveContext() {
    // std::cerr << "encodeActiveContext: ";
    // activeContext->print(std::cerr);
    return encodeContext(*activeContext);
  }

  std::vector<ContextId> decodeContext(ContextHash hash) {
    if (contextMap.count(hash)) {
      return contextMap[hash];
    } else {
      assert(false && "Could not find the context");
      return {};
    }
  }

};
}; // namespace SpecPrivLib

} // namespace SLAMPLib
#endif // SLAMPLIB_HOOKS_SLAMP_CONTEXT_H
