#pragma once

#include "ProfilingModules/context.h"
#include <cassert>
#include <iostream>
#include <stack>
#include <unordered_map>
#include <utility>
#include <map>
#include <vector>


// namespace SpecPrivLib {
// enum SpecPrivContextType {
  // TopContext = 0,
  // FunctionContext,
  // LoopContext,
// };

#define CONTEXT_DEBUG 0

template <class TypeEnum, typename MetaIdType>
struct ContextId {
  static_assert(TypeEnum::TopContext == 0, "TopContext must be 0");

  TypeEnum type;
  MetaIdType metaId;
  ContextId(TypeEnum type, MetaIdType id) : type(type), metaId(id) {}
  ContextId(std::pair<TypeEnum, MetaIdType> p)
      : type(p.first), metaId(p.second) {}
  static ContextId getTopContextId() { return {TypeEnum::TopContext, 0}; }
  // id left shift 2 bits and or with type
  // ContextHash hash() {
    // return (static_cast<ContextHash>(metaId) << 2) | static_cast<ContextHash>(type);
  // }

  bool operator==(const ContextId &other) const {
    return type == other.type && metaId == other.metaId;
  }

  bool operator!=(const ContextId &other) const {
    return !(*this == other);
  }

  bool operator<(const ContextId &other) const {
    return type < other.type || (type == other.type && metaId < other.metaId);
  }

  void print(std::ostream &os) {
    os << "(" << type << "," << metaId << ")";
  }
};

template <class TypeEnum, typename MetaIdType, typename HashType = size_t>
class NewContextManager {
  using ContextId = ContextId<TypeEnum, MetaIdType>;
  std::vector<ContextId> contextStack;

  // FIXME: fix this hashing problem
  std::map<std::vector<ContextId>, HashType> contextToHashMap;
  std::vector<std::vector<ContextId>> hashToContextMap;
  size_t contextIdHashCounter = 1; // avoid 0
  bool cached = false;
  HashType cachedContextHash;

public:
  NewContextManager() {
    contextStack.push_back(ContextId::getTopContextId());
    hashToContextMap.push_back(contextStack);
  }

  ~NewContextManager() = default;

  void pushContext(ContextId contextId) {
    cached = false;
    contextStack.push_back(contextId); 
  }

  void popContext(ContextId contextId) {
    cached = false;
    if (contextStack.back() == contextId) {
      contextStack.pop_back();
    } else {
      if (CONTEXT_DEBUG) {
        std::cerr << "ContextManager: popContext: context not found: ";
        contextId.print(std::cerr);
        std::cerr << "ContextManager: popContext: stack: ";
        for (auto &c : contextStack) {
          c.print(std::cerr);
        }
        std::cerr << "\n";
      }
      // keep popping until we find the context
      while (contextStack.back() != contextId && contextStack.back() != ContextId::getTopContextId()) {
        contextStack.pop_back();
      }

      if (contextStack.back() == contextId) {
        contextStack.pop_back();
      } else {
        if (CONTEXT_DEBUG) {
          std::cerr << "ContextManager: popContext: context not found: ";
        }
      }
    }
  }

  HashType encodeContext(std::vector<ContextId> context) {
    if (contextToHashMap.count(context)) {
      return contextToHashMap[context];
    } else {
      contextToHashMap[context] = contextIdHashCounter;
      hashToContextMap.push_back(context);

      return contextIdHashCounter++;
    }
  }

  HashType encodeActiveContext() {
    if (cached) {
      return cachedContextHash;
    } else {
      cached = true;
      cachedContextHash = encodeContext(contextStack);
      return cachedContextHash;
    }
  }

  std::vector<ContextId> decodeContext(HashType hash) {
    assert(hash < hashToContextMap.size() && hash != 0 && "invalid hash");
    return hashToContextMap[hash];
  }

  void printContext(std::ostream &os, HashType hash) {
    assert(hash < hashToContextMap.size() && hash != 0 && "invalid hash");
    auto &context = hashToContextMap[hash];
    for (auto it = context.rbegin(); it != context.rend(); it++) {
      auto &c = *it;
      c.print(os);
    }
  }
};
