#include <cstdint>
#include <unordered_set>
#include <vector>

#include "slamp_logger.h"
#include "slamp_shadow_mem.h"
#include "slamp_timestamp.h"

#include "LocalWriteModule.h"
#include "HTContainer.h"
#include "context.h"

enum class ObjectLifetimeModAction : uint32_t {
  INIT = 0,
  // LOAD,
  // STORE,
  ALLOC,
  FREE,
  LOOP_INVOC,
  LOOP_ITER,
  LOOP_EXIT,
  FUNC_ENTRY,
  FUNC_EXIT,
  FINISHED
};

using namespace SLAMPLib;

class ObjectLifetimeModule: public LocalWriteModule {
  private:
    uint64_t slamp_iteration = 0;
    uint64_t slamp_invocation = 0;
    slamp::MemoryMap *smmap = nullptr;
    uint32_t target_loop_id = 0;

    bool in_loop = false;

    SpecPrivLib::SpecPrivContextManager contextManager;
    HTSet<uint64_t, std::hash<uint64_t>, std::equal_to<>, 8> shortLivedObjects, longLivedObjects;
    // std::unordered_set<unsigned long> shortLivedObjects, longLivedObjects;

  public:
  ObjectLifetimeModule(uint32_t mask, uint32_t pattern)
      : LocalWriteModule(mask, pattern) {
    smmap = new slamp::MemoryMap(LOCALWRITE_MASK, LOCALWRITE_PATTERN, TIMESTAMP_SIZE_IN_BYTES);
  }

  ~ObjectLifetimeModule() override { 
    delete smmap; 
  }

  void init(uint32_t loop_id, uint32_t pid);
  void fini(const char *filename);

  void allocate(void *addr, uint32_t instr, uint64_t size);
  void free(void *addr);

  void func_entry(uint32_t fcnId);
  void func_exit(uint32_t fcnId);

  void loop_invoc();
  void loop_iter();
  void loop_exit();

};
