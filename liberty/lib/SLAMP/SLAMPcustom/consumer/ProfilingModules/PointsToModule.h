#include <cstdint>
#include <unordered_set>
#include <vector>

#include "slamp_logger.h"
#include "slamp_shadow_mem.h"
#include "slamp_timestamp.h"

#include "LocalWriteModule.h"
#include "HTContainer.h"
#include "context.h"

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

using namespace SLAMPLib;

class PointsToModule : public LocalWriteModule {
  private:
    uint64_t slamp_iteration = 0;
    uint64_t slamp_invocation = 0;
    slamp::MemoryMap *smmap = nullptr;
    uint32_t target_loop_id = 0;

    bool in_loop = false;

    SpecPrivLib::SpecPrivContextManager *contextManager;
    std::unordered_set<unsigned long> *shortLivedObjects, *longLivedObjects;
    std::unordered_set<ContextHash> *targetLoopContexts;
    using SlampAllocationUnit = TS;
    std::unordered_map<uint64_t, std::unordered_set<SlampAllocationUnit>> *pointsToMap;

  public:
  PointsToModule(uint32_t mask, uint32_t pattern)
      : LocalWriteModule(mask, pattern) {
    smmap = new slamp::MemoryMap(LOCALWRITE_MASK, LOCALWRITE_PATTERN, TIMESTAMP_SIZE_IN_BYTES);

    contextManager = new SpecPrivLib::SpecPrivContextManager();
    shortLivedObjects = new std::unordered_set<uint64_t>();
    longLivedObjects = new std::unordered_set<uint64_t>();
    targetLoopContexts = new std::unordered_set<uint64_t>();
    pointsToMap = new std::unordered_map<uint64_t, std::unordered_set<SlampAllocationUnit>>();
  }

  ~PointsToModule() override { 
    delete smmap; 
    delete contextManager;
    delete shortLivedObjects;
    delete longLivedObjects;
    delete targetLoopContexts;
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
};
