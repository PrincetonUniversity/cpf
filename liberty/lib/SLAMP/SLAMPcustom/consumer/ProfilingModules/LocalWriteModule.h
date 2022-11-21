#pragma once
#include <cstdint>

class LocalWriteModule {
protected:
  const uint32_t LOCALWRITE_MASK{};
  const uint32_t LOCALWRITE_PATTERN{};
  // PAGE SIZE 4096 = 2^12; FIXME: here we can make it finer because we
  // allocate 8 bytes of metadata per one byte of data, but changes to
  // shadow memory needed
  static constexpr uint32_t LOCALWRITE_SHIFT = 12;
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

