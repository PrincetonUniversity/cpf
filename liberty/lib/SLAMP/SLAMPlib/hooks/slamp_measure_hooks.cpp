#include "malloc.h"
#include "slamp_hooks.h"
#include <cstdint>
#include <fstream>
#include <string>

using namespace std;
static void *(*old_malloc_hook)(unsigned long, const void *);
static void (*old_free_hook)(void *, const void *);

static double total_malloc_size = 0;
static double total_load_size = 0;
static double total_store_size = 0;

void SLAMP_measure_init() {
  // replace hooks
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  __malloc_hook = SLAMP_measure_malloc_hook;
  __free_hook = SLAMP_measure_free_hook;
}

void SLAMP_measure_fini() {
  string filename = "slamp.measure.txt";
  ofstream of(filename);

  of << "Total Malloc Size: " << total_malloc_size << "\n"
     << "Total Load Size: " << total_load_size << "\n"
     << "Total Store Size: " << total_store_size << "\n";
  of.close();
}

void *SLAMP_measure_malloc_hook(size_t size, const void *caller) {
  total_malloc_size += size;

  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;

  auto ptr = malloc(size);

  __malloc_hook = SLAMP_measure_malloc_hook;
  __free_hook = SLAMP_measure_free_hook;
  return ptr;
}

// TODO: nothing yet here
void SLAMP_measure_free_hook(void *ptr, const void *caller) {
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  free(ptr);
  __malloc_hook = SLAMP_measure_malloc_hook;
  __free_hook = SLAMP_measure_free_hook;
}

// TODO: nothing for id yet
void SLAMP_measure_load(uint32_t id, uint64_t size) { total_load_size += size; }

// TODO: nothing for id yet
void SLAMP_measure_store(uint32_t id, uint64_t size) {
  total_store_size += size;
}
