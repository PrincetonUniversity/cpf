#include <cstdint>
#include <unordered_set>
#include <vector>

#include "ProfilingModules/parallel_hashmap/phmap.h"
#include "slamp_logger.h"
#include "slamp_shadow_mem.h"
#include "slamp_timestamp.h"

#include "LocalWriteModule.h"
#include "HTContainer.h"
#include "ContextManager.h"

enum class PointsToModAction : uint32_t {
  INIT = 0,
  // LOAD,
  // STORE,
  ALLOC,
  FREE,
  LOOP_INVOC,
  LOOP_ITER,
  LOOP_ENTRY,
  LOOP_EXIT,
  FUNC_ENTRY,
  FUNC_EXIT,
  POINTS_TO_INST,
  POINTS_TO_ARG,
  FINISHED
};

class PointsToModule : public LocalWriteModule {
  private:
    uint64_t slamp_iteration = 0;
    uint64_t slamp_invocation = 0;
    slamp::MemoryMap<MASK2_PT> *smmap = nullptr;
    uint32_t target_loop_id = 0;

    bool in_loop = false;

    enum SpecPrivContextType {
      TopContext = 0,
      FunctionContext,
      LoopContext,
    };
    using ContextHash = uint64_t;
    using SpecPrivContextManager = NewContextManager<SpecPrivContextType, uint32_t, ContextHash>;
    using ContextId = ContextId<SpecPrivContextType, uint32_t>;
    std::unordered_set<ContextHash> targetLoopContexts;
    SpecPrivContextManager contextManager;

    using SlampAllocationUnit = TS;
    // std::unordered_map<uint64_t, std::unordered_set<SlampAllocationUnit>> pointsToMap;
    // HTMap_Set<uint64_t, SlampAllocationUnit, std::hash<uint64_t>, std::equal_to<>, 32> pointsToMap;
    // phmap::flat_hash_map<uint64_t, phmap::flat_hash_set<SlampAllocationUnit>> pointsToMap;
    HTMap_IsConstant<uint64_t> pointsToMap;

    using InstrAndContext = std::pair<uint32_t, std::vector<ContextId>>;
    std::map<InstrAndContext, InstrAndContext> decodedContextMap;
    // std::map<InstrAndContext, std::set<InstrAndContext>> decodedContextMap;

  public:
  PointsToModule(uint32_t mask, uint32_t pattern)
      : LocalWriteModule(mask, pattern) {
    smmap = new slamp::MemoryMap<MASK2_PT>(mask, pattern, TIMESTAMP_SIZE_IN_BYTES);
  }

  ~PointsToModule() override { 
    delete smmap; 
  }

  void init(uint32_t loop_id, uint32_t pid);
  void fini(const char *filename);

  void allocate(void *addr, uint32_t instr, uint64_t size);
  void free(void *addr);
  void loop_invoc();
  void loop_iter();

  void func_entry(uint32_t fcnId);
  void func_exit(uint32_t fcnId);
  void loop_entry(uint32_t loopId);
  void loop_exit(uint32_t loopId);

  void points_to_inst(uint32_t instId, void *ptr);

  void points_to_arg(uint32_t fcnId, uint32_t argId, void *ptr);
  void merge(PointsToModule &other);
  void decode_all();
};
