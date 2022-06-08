#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <slamp_bound_malloc.h>

// Round-up/-down to a power of two
#define ROUND_DOWN(n, k) ((~((k)-1)) & (uint64_t)(n))
#define ROUND_UP(n, k) ROUND_DOWN((n) + ((k)-1), (k))

// Align all allocation units to a multiple of this.
// MUST BE A POWER OF TWO
#define ALIGNMENT (16)

// GET SIZE OF THE OBJECT
#define GET_SIZE(ptr) *((size_t *)((uint64_t)(ptr) - sizeof(size_t)))
#define RESET_SIZE(ptr, sz)                                                    \
  (*((size_t *)((uint64_t)(ptr) - sizeof(size_t))) = (sz))

#define OBJ_BEGIN(ptr) ((uint64_t)(ptr) - 2 * sizeof(size_t))
#define OBJ_END(ptr, size) ((uint64_t)(ptr) + size - 1)

namespace slamp {

const static size_t unit_sz = 0x100000000L;
static size_t unit_mask;
static uint64_t heap_begin;
static uint64_t heap_end;
static uint64_t heap_next;
static size_t pagesize;
static size_t pagemask;

std::unordered_map<uint64_t, unsigned> *page_alive_ptr_count;

/* make bound_malloc to return the address from the next page */

// preallocate a huge heap
void init_bound_malloc(void *heap_bound) {
  unit_mask = ~(unit_sz - 1);

  auto a = reinterpret_cast<uint64_t>(sbrk(0));
  heap_begin = (a + (unit_sz - 1)) & unit_mask;
  heap_end = reinterpret_cast<uint64_t>(heap_bound);

  for (uint64_t addr = heap_begin; addr < heap_end; addr += unit_sz) {
    void *p =
        mmap(reinterpret_cast<void *>(addr), unit_sz, PROT_WRITE | PROT_READ,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) {
      // fprintf(stderr, "mmap failed with %p\n", (void*)addr);
      // perror("mmap failed for bound_malloc_init\n");
      // exit(0);
      int err = errno;
      printf("mmap failed: %lx errno: %d\n", addr, err);
      raise(SIGINT);

      heap_end = addr;
      break;
    }
  }

  heap_next = heap_begin;

  // remember pagesize to support bound_discard_page()
  pagesize = getpagesize();
  pagemask = ~(pagesize - 1);

  page_alive_ptr_count = new std::unordered_map<uint64_t, unsigned>();

  // fprintf(stderr, "init_bound_malloc %lx to %lx\n", heap_begin, heap_end);
}

void fini_bound_malloc() {
  for (uint64_t addr = heap_begin; addr < heap_end; addr += unit_sz)
    munmap((void *)addr, unit_sz);
  delete page_alive_ptr_count;
}

size_t get_object_size(void *ptr) { return GET_SIZE(ptr); }

void *bound_malloc(size_t size, size_t alignment) {
  // allocation unit size
  size_t sz = ROUND_UP(size, alignment);

  // if alignment is not power of two, error and exit
  if (alignment & (alignment - 1)) {
    fprintf(stderr, "alignment must be power of two\n");
    exit(-1);
  }

  size_t ret = ROUND_UP(heap_next + sizeof(size_t), alignment);

  heap_next = ret - sizeof(size_t);
  // store size of the unit
  auto *sz_ptr = (size_t *)(heap_next);
  *sz_ptr = sz;

  heap_next += sizeof(size_t) + sz;

  if (heap_next >= heap_end) {
    perror("Error: bound_malloc, not enough memory\n");
    exit(-1);
  }

  auto a = reinterpret_cast<uint64_t>(ret);
  uint64_t pagebegin = OBJ_BEGIN(a) & pagemask;
  uint64_t pageend = OBJ_END(a, size) & pagemask;

  // increate the page alive count
  for (auto page = pagebegin; page <= pageend; page += pagesize) {
    auto it = page_alive_ptr_count->find(page);
    if (it == page_alive_ptr_count->end()) {
      page_alive_ptr_count->insert(std::make_pair(page, 1));
    } else {
      it->second++;
    }
  }

  return (void *)ret;
}

bool bound_free(void *ptr, uint64_t &starting_page, unsigned &purge_cnt) {
  
  // free nullptr has no effect
  if (ptr == nullptr)
    return false;

  // if not within heap bound, ignore it
  auto a = reinterpret_cast<uint64_t>(ptr);
  if (a < heap_begin || a >= heap_end)
    return false;

  bool purge = false;
  purge_cnt = 0;

  auto size = GET_SIZE(ptr);
  uint64_t pagebegin = OBJ_BEGIN(a) & pagemask;
  uint64_t pageend = OBJ_END(a, size) & pagemask;

  // unmap the page if no pointer is alive
  for (auto page = pagebegin; page <= pageend; page += pagesize) {

    // update the map
    auto it = page_alive_ptr_count->find(page);
    if (it == page_alive_ptr_count->end()) {
      continue;
    }
    it->second--;

    // no alive pointer left
    if (it->second == 0) {
      // we can unmap if the heap_next has passed the page end
      if (heap_next >= page + pagesize) {
        munmap((void *)page, pagesize);
        purge_cnt++;

        // fprintf(stderr, "bound_free: unmap %lx\n", page);
        if (!purge) {
          purge = true;
          starting_page = page;
        }
      }
    }
  }

  return purge;
}

void *bound_calloc(size_t num, size_t size) {
  void *ptr = bound_malloc(num * size);
  memset(ptr, '\0', num * size);
  return ptr;
}

void *bound_realloc(void *ptr, size_t size) {
  if (ptr == nullptr) {
    /*
       In case that ptr is a null pointer, the function behaves like malloc,
       assigning a new block of size bytes and returning a pointer to its
       beginning.
     */
    return bound_malloc(size);
  }

  if (size == 0) {
    /*
       If size is zero, the return value depends on the particular library
       implementation (it may or may not be a null pointer), but the returned
       pointer shall not be used to dereference an object in any case.
     */

    // FIXME: free the memory
    uint64_t starting_page;
    unsigned purge_cnt;
    bound_free(ptr, starting_page, purge_cnt);
    return nullptr;
  }

  size_t old_sz = GET_SIZE(ptr);
  if (old_sz >= size) {
    RESET_SIZE(ptr, size);
    return ptr;
  } else {
    // do the same thing as malloc
    size_t sz = ROUND_UP(size, ALIGNMENT);

    // FIXME: offset 8 so the return address can be 16 byte aligned
    heap_next += sizeof(size_t);
    auto *sz_ptr = (size_t *)(heap_next);
    *sz_ptr = sz;
    heap_next += sizeof(size_t);

    void *new_ptr = (size_t *)(heap_next);
    heap_next += sz;

    // if there is not enough memory, just return the original pointer
    if (heap_next > heap_end) {
      heap_next -= sz;
      heap_next -= sizeof(size_t);
      // FIXME: offset 8 so the return address can be 16 byte aligned
      heap_next -= sizeof(size_t);
      return ptr;
    }

    memcpy(new_ptr, ptr, old_sz);
    return new_ptr;
  }
}

void bound_discard_page() {
  heap_next = (heap_next + (pagesize - 1)) & pagemask;
  if (heap_next >= heap_end) {
    perror("Error: bound_malloc, not enough memory\n");
    exit(0);
  }
}

} // namespace slamp
