#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "slamp_logger.h"
#include "slamp_shadow_mem.h"
#include "slamp_timestamp.h"

#include "HTContainer.h"

enum DepModAction : char {
  INIT = 0,
  LOAD,
  STORE,
  ALLOC,
  LOOP_INVOC,
  LOOP_ITER,
  FINISHED
};

class LocalWriteModule {
protected:
  const uint32_t LOCALWRITE_MASK{};
  const uint32_t LOCALWRITE_PATTERN{};
  static constexpr uint32_t LOCALWRITE_SHIFT = 12; // PAGE SIZE 4096 = 2^12
  // takes in a lambda action and uint64_t addr
  template <typename F>
  inline void local_write(uint64_t addr, const F &action) {
    if (((addr >> LOCALWRITE_SHIFT) & LOCALWRITE_MASK) == LOCALWRITE_PATTERN) {
      action();
    }
  }

public:
  LocalWriteModule(uint32_t mask, uint32_t pattern)
      : LOCALWRITE_MASK(mask), LOCALWRITE_PATTERN(pattern) {}
  virtual ~LocalWriteModule() = default;
};

class DependenceModule : public LocalWriteModule {
private:
  uint64_t slamp_iteration = 0;
  uint64_t slamp_invocation = 0;
  uint32_t target_loop_id = 0;

  // debugging stats
  uint64_t load_count = 0;
  uint64_t store_count = 0;

  slamp::MemoryMap *smmap = nullptr;

  HTSet<slamp::KEY, slamp::KEYHash, slamp::KEYEqual, 8> dep_set;

  void log(TS ts, const uint32_t dst_inst, const uint32_t bare_inst,
           const uint64_t load_invocation, const uint64_t load_iteration);

public:
  DependenceModule(uint32_t mask, uint32_t pattern)
      : LocalWriteModule(mask, pattern) {
    smmap = new slamp::MemoryMap(LOCALWRITE_MASK, LOCALWRITE_PATTERN, TIMESTAMP_SIZE_IN_BYTES);
  }

  void init(uint32_t loop_id, uint32_t pid);
  void fini(const char *filename);
  // always_inline attribute
  void load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr,
            uint64_t value);
  void store(uint32_t instr, uint32_t bare_instr, const uint64_t addr);
  void allocate(void *addr, uint64_t size);
  void loop_invoc();
  void loop_iter();
};
