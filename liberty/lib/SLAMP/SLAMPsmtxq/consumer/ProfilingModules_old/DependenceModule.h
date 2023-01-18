#include <cstdint>

namespace DepMod {
void init(uint32_t loop_id, uint32_t pid);
void fini(const char *filename);
void load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value);
void store(uint32_t instr, uint32_t bare_instr, const uint64_t addr);
void allocate(void *addr, uint64_t size);
void loop_invoc();
void loop_iter();

enum DepModAction: char
{
    INIT = 0,
    LOAD,
    STORE,
    ALLOC,
    LOOP_INVOC,
    LOOP_ITER,
    FINISHED
};

} // namespace DepMod
