#ifndef SLAMPLIB_HOOKS_SLAMP_LOGGER
#define SLAMPLIB_HOOKS_SLAMP_LOGGER

#include <cstdint>
#include <stdint.h>
#include <stdlib.h>
#include <functional>

#include "slamp_timestamp.h"

namespace slamp
{

struct KEY
{
  uint32_t src;
  uint32_t dst;
  uint32_t dst_bare;
  uint32_t cross;

  KEY() {}
  KEY(uint32_t s, uint32_t d, uint32_t b, uint32_t c) : src(s), dst(d), dst_bare(b), cross(c) {}
};

struct KEYHash
{
  size_t operator()(const KEY& key) const
  {
    static_assert(sizeof(size_t) == sizeof(uint64_t), "Should be 64bit address");
    std::hash<uint64_t> hash_fn;

    // hash(32-32) ^ hash(32-1)
    // FIXME: find a better hash function
    return hash_fn(((uint64_t)key.src << 32) | key.dst) ^ hash_fn(((uint64_t)key.dst_bare << 1) | (key.cross & 0x1));
  }
};

struct KEYComp
{
  bool operator()(const KEY& key1, const KEY& key2) const
  {
    uint32_t src1 = key1.src;
    uint32_t src2 = key2.src;

    if (src1 < src2)
      return true;
    else if (src1 > src2)
      return false;

    uint32_t dst1 = key1.dst;
    uint32_t dst2 = key2.dst;

    if (dst1 < dst2)
      return true;
    else if (dst1 > dst2)
      return false;

    uint32_t dst_bare1 = key1.dst_bare;
    uint32_t dst_bare2 = key2.dst_bare;

    if (dst_bare1 < dst_bare2)
      return true;
    else if (dst_bare1 > dst_bare2)
      return false;

    uint32_t cross1 = key1.cross;
    uint32_t cross2 = key2.cross;

    return cross1 < cross2;
  }
};

struct KEYEqual
{
  bool operator()(const KEY& key1, const KEY& key2) const
  {
    uint32_t src1 = key1.src;
    uint32_t src2 = key2.src;

    if ( src1 != src2 ) return false;

    uint32_t dst1 = key1.dst;
    uint32_t dst2 = key2.dst;

    if ( dst1 != dst2 ) return false;

    uint32_t dst_bare1 = key1.dst_bare;
    uint32_t dst_bare2 = key2.dst_bare;

    if ( dst_bare1 != dst_bare2 ) return false;

    uint32_t cross1 = key1.cross;
    uint32_t cross2 = key2.cross;

    if ( cross1 != cross2 ) return false;

    return true;
  }
};


void init_logger(uint32_t fn_id, uint32_t loop_id);
void fini_logger(const char* filename);

uint32_t log(TS ts, const uint32_t dst_instr, TS* pts, const uint32_t bare_inst, uint64_t addr, uint64_t value, uint8_t size);
void print_log(const char* filename);

}

#endif
