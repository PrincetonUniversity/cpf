#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "slamp_logger.h"
#include "slamp_shadow_mem.h"
#include "slamp_timestamp.h"

#include "LocalWriteModule.h"
#include "HTContainer.h"

#define TRACK_COUNT
#define TRACK_MIN_DISTANCE


#define TRACK_WAW
#define TRACK_WAR

#ifdef TRACK_WAR
#define DM_TIMESTAMP_SIZE_IN_BYTES 16
#define DM_TIMESTAMP_SIZE_IN_BYTES_LOG2 4
#else
#define DM_TIMESTAMP_SIZE_IN_BYTES 8
#define DM_TIMESTAMP_SIZE_IN_BYTES_LOG2 3
#endif

enum DepModAction : uint32_t {
  INIT = 0,
  LOAD,
  STORE,
  ALLOC,
  LOOP_INVOC,
  LOOP_ITER,
  FINISHED
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

#ifdef TRACK_COUNT
  HTMap_Sum<slamp::KEY, slamp::KEYHash, slamp::KEYEqual, 16> deps;
#else
  HTSet<slamp::KEY, slamp::KEYHash, slamp::KEYEqual, 16> deps;
#endif

#ifdef TRACK_MIN_DISTANCE
  HTMap_Min<slamp::KEY, slamp::KEYHash, slamp::KEYEqual, 16> min_dist;
#endif

  void log(TS ts, const uint32_t dst_inst, const uint32_t bare_inst);

public:
  DependenceModule(uint32_t mask, uint32_t pattern)
      : LocalWriteModule(mask, pattern) {
    smmap = new slamp::MemoryMap(mask, pattern, DM_TIMESTAMP_SIZE_IN_BYTES);
  }

  ~DependenceModule() override { delete smmap; }

  void init(uint32_t loop_id, uint32_t pid);
  void fini(const char *filename);
  // always_inline attribute
  void load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr);
  void store(uint32_t instr, uint32_t bare_instr, const uint64_t addr);
  void allocate(void *addr, uint64_t size);
  void loop_invoc();
  void loop_iter();

  void merge_dep(DependenceModule &other);
};
