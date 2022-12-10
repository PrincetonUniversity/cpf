#ifndef SLAMPLIB_HOOKS_SLAMP_SHADOW_MEM_H
#define SLAMPLIB_HOOKS_SLAMP_SHADOW_MEM_H

#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include <iostream>
#include <set>
#include <unordered_map>

// higher half of canonical region cannot be used

/// left shift by `shift`, mask 47 LSB, toggle #45 bit?
#define MASK1 0x00007fffffffffffL
#define MASK2 0x0000200000000000L
#define GET_SHADOW(addr, shift)                                                \
  (((((uint64_t)(addr)) << (shift)) & MASK1) ^ MASK2)

namespace slamp {

class MemoryMap {
public:
  uint64_t heapStart = 0;
  MemoryMap(unsigned r) : ratio(r), ratio_shift(0) {
    // ratio expected to be a power of 2
    assert((r & (r - 1)) == 0);

    // log(r)
    unsigned n = r;
    while ((n & 1) == 0) {
      this->ratio_shift += 1;
      n = n >> 1;
    }

    // get the page size of the host system
    pagesize = getpagesize();
    pagemask = ~(pagesize - 1);
  }

  ~MemoryMap() {
    // freeing all remaining shadow addresses
    for (auto page : pages) {
      uint64_t s = GET_SHADOW(page, ratio_shift);
      munmap(reinterpret_cast<void *>(s), pagesize * ratio);
    }
  }

  unsigned get_ratio() { return ratio; }

  bool is_allocated(void *addr) {
    auto a = reinterpret_cast<uint64_t>(addr);
    uint64_t page = a & pagemask;
    if (pages.find(page) != pages.end())
      return true;
    else
      return false;
  }

  /// allocate shadow page if not exist
  void *allocate(void *addr, size_t size) {
    auto a = reinterpret_cast<uint64_t>(addr);
    uint64_t pagebegin = a & pagemask;
    uint64_t pageend = (a + size - 1) & pagemask;

    // try mmap

    std::set<void *> shadow_pages;
    bool success = true;

    for (uint64_t page = pagebegin; page <= pageend; page += pagesize) {
      if (pages.find(page) != pages.end())
        continue;

      uint64_t s = GET_SHADOW(page, ratio_shift);
      // create a shadow page for the page
      void *p = mmap(reinterpret_cast<void *>(s), pagesize * ratio,
                     PROT_WRITE | PROT_READ,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
      if (p == MAP_FAILED) {
        int err = errno;
        printf("mmap failed: %lx errno: %d\n", s, err);
        raise(SIGINT);

        success = false;
        break;
      } else {
        shadow_pages.insert(p);
      }
    }

    if (success) {
      for (uint64_t page = pagebegin; page <= pageend; page += pagesize)
        pages.insert(page);
      uint64_t pagebegin = a & pagemask;
      uint64_t pageend = (a + size - 1) & pagemask;

      // return shadow_mem
      auto *shadow_addr = (uint64_t *)GET_SHADOW(a, ratio_shift);
      return (void *)(shadow_addr);
    } else {
      // cleanup
      for (auto shadow_page : shadow_pages)
        munmap(shadow_page, pagesize * ratio);
      return nullptr;
    }
  }

  // free the shadow pages
  void deallocate_pages(uint64_t page, unsigned cnt) {
    munmap(reinterpret_cast<void *>(GET_SHADOW(page, ratio_shift)),
           pagesize * ratio * cnt);

    // fprintf(stderr, "deallocate_pages: %lx %d\n", GET_SHADOW(page, ratio_shift), cnt);

    for (auto i = 0; i < cnt; i++, page += pagesize)
      pages.erase(page);
  }

  /// for realloc; the dependence carries over
  void copy(void *dst, void *src, size_t size) {
    size_t shadow_size = size * ratio;
    void *shadow_dst = (void *)GET_SHADOW(dst, ratio_shift);
    void *shadow_src = (void *)GET_SHADOW(src, ratio_shift);
    memcpy(shadow_dst, shadow_src, shadow_size);
  }

  void init_heap(void *addr) {
    heapStart = reinterpret_cast<uint64_t>(addr);
  }

  // init stack size to be fixed 8MB
  // the stack in /proc/$pid/maps changes in runtime, use it to find the end
  void init_stack(uint64_t stack_size, uint64_t pid) {
    char filename[256];
    char buf[5000];
    sprintf(filename, "/proc/%lu/maps", pid);
    // sprintf(filename, "/proc/%u/maps", getpid());

    FILE *fp = fopen(filename, "r");
    if (!fp) {
      perror(filename);
      exit(EXIT_FAILURE);
    }

    bool allocated = false;

    while (fgets(buf, sizeof(buf), fp) != nullptr) {
      uint64_t start, end;
      char name[5000];

      int n = sscanf(buf, "%lx-%lx %*c%*c%*c%*c %*llx %*x:%*x %*lu %s", &start,
                     &end, name);

      if (n != 3) {
        continue;
      }

      // FIXME: get heap start addr
      if (!strcmp(name, "[heap]")) {
        heapStart = start;
      }


      if (!strcmp(name, "[stack]")) {
        // stack grow from end (big address) backwards
        // print the stack start and end
        printf("stack: %lx %lx\n", end - stack_size, end);
        allocate(reinterpret_cast<void *>(end - stack_size), stack_size);
        allocated = true;
        break;
      }
    }

    if (!allocated) {
      fprintf(stderr, "Error: failed to allocate shadow for stack");
      exit(EXIT_FAILURE);
    }
  }

private:
  std::set<uint64_t> pages; // page table

  unsigned ratio; // (size of metadata) / (size of real data)
  unsigned ratio_shift;
  uint64_t pagesize;
  uint64_t pagemask;
};

} // namespace slamp
#endif
