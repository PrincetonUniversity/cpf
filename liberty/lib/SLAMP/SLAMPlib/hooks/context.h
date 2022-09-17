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

#include <stack>
#include <utility>
#include "llvm/IR/Instruction.h"
#include "scaf/Utilities/Metadata.h"

namespace SLAMPLib {

/// The context is stack of the ContextId
template <typename ContextId> struct Context {
  ContextId id;
  Context *parent;

  Context(ContextId id, Context *parent) : id(std::move(id)), parent(parent) {}

  /// Add a new context to the stack
  Context *chain(ContextId id) { return new Context(id, this); }

  size_t hash() {
    size_t h = id.hash();
    if (parent)
      h ^= parent->hash();
  }

  /// Remove the top context from the stack
  Context *pop() {
    auto parent = this->parent;
    // FIXME: is this ok to use?
    delete this;
    return parent;
  }
  using FlattenContext = std::vector<ContextId>;

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
};

template <class Context>
struct ContextManager {
  Context *activeContext;

  ContextManager() : activeContext(nullptr) {}
  std::unordered_map <size_t, typename Context::FlattenContext> contextMap;

  void addContext(size_t hash, Context context) {
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
  // id left shift 2 bits and or with type
  size_t hash() {
    return (static_cast<size_t>(metaId) << 2) | static_cast<size_t>(type);
  }

  bool operator==(const ContextId &other) const {
    return type == other.type && metaId == other.metaId;
  }
};
using Context = Context<ContextId>;
}; // namespace SpecPrivLib


} // namespace SLAMPLib
#endif // SLAMPLIB_HOOKS_SLAMP_CONTEXT_H
