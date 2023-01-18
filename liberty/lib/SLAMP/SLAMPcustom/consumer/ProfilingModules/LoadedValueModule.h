#pragma once
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "LocalWriteModule.h"
#include "HTContainer.h"
#include "context.h"

enum class LoadedValueModAction : uint32_t {
  INIT = 0,
  LOAD,
  FINISHED
};

struct PairHash
{
    std::size_t operator () (std::pair<uint32_t, uint32_t> const &v) const
    {
      static_assert(sizeof(size_t) == sizeof(uint64_t), "Should be 64bit address");
      // std::hash<uint32_t> hash_fn;
      // need to make sure if the pair are the same, this doesn't generate 0
      return ((uint64_t)v.first << 32) | v.second;
    }
};

// instr, bare_instr
using AccessKey = std::pair<uint32_t, uint32_t>;
struct Constant {
  bool valid;
  // bool valueinit;
  // uint8_t size;
  // uint64_t addr;
  uint64_t value;
  // char pad[64 - sizeof(uint64_t) - sizeof(uint64_t) - sizeof(uint8_t) -
  //          sizeof(bool) - sizeof(bool)];

  Constant(bool valid, uint64_t value) : valid(valid), value(value) {}
  // Constant(bool va, bool vi, uint8_t s, uint64_t a, uint64_t v)
  //     : valid(va), valueinit(vi), size(s), addr(a), value(v) {}
};


class LoadedValueModule: public GenericLocalWriteModule {
  private:
    uint64_t slamp_iteration = 0;
    uint64_t slamp_invocation = 0;
    uint32_t target_loop_id = 0;

    bool in_loop = false;

    // std::unordered_map<AccessKey, Constant *, PairHash> *constmap_value;
    HTMap_IsConstant<AccessKey, PairHash>  constmap_value;

  public:
    LoadedValueModule(uint32_t mask, uint32_t pattern)
      : GenericLocalWriteModule(mask, pattern) {
  }
 
  ~LoadedValueModule() override = default;

  void init(uint32_t loop_id, uint32_t pid);
  void fini(const char *filename);
  void load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value, uint8_t size);

  void merge_values(LoadedValueModule &other);
};
