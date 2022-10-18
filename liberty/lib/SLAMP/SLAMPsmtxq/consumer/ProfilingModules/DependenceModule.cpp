#include <cstdint>
#include <set>
#include <thread>
#include <vector>
#include <unordered_set>
#include <fstream>

#include "slamp_timestamp.h"
#include "slamp_logger.h"
#include "slamp_shadow_mem.h"
#include "DependenceModule.h"
#include <mutex>
std::mutex m;

#define SIZE_8M  0x800000

#define DEPLOG_VEC_SIZE 100000000

static slamp::MemoryMap* smmap = nullptr;
static std::unordered_set<slamp::KEY, slamp::KEYHash, slamp::KEYEqual> *deplog_set;
static std::vector<slamp::KEY> *deplog_vec;
static unsigned long deplog_vec_counter = 0;
static uint64_t slamp_iteration = 0;
static uint64_t slamp_invocation = 0;
static uint32_t target_loop_id = 0;

static void convertVectorToSet() {

  // launch 56 threads to convert the vector to set independently, chunking
  constexpr auto THREADS = 56;
  std::thread t[THREADS];
  for (unsigned long i = 0; i < THREADS; i++) {
    t[i] = std::thread(
        [&](int id) {
          m.lock();
          // take the chunk and convert to a set and return
          auto *deplog_set_chunk =
              new std::unordered_set<slamp::KEY, slamp::KEYHash,
                                     slamp::KEYEqual>();
          deplog_set_chunk->reserve(DEPLOG_VEC_SIZE / THREADS + 1);
          auto begin = id * (DEPLOG_VEC_SIZE / THREADS);
          auto end = (id + 1) * (DEPLOG_VEC_SIZE / THREADS);
          deplog_set_chunk->insert(deplog_vec->begin() + begin,
                                   deplog_vec->begin() + end);

          // lock the global set and insert the chunk
          deplog_set->insert(deplog_set_chunk->begin(),
                             deplog_set_chunk->end());
          m.unlock();
          delete deplog_set_chunk;
        },
        i);
  }
  // join the threads
  for (auto &i : t) {
    i.join();
  }

  std::cout << "Merging vec to set, set length " << deplog_set->size()
            << std::endl;
}
namespace DepMod {
// init: setup the shadow memory
void init(uint32_t loop_id, uint32_t pid) {

  target_loop_id = loop_id;
  smmap = new slamp::MemoryMap(TIMESTAMP_SIZE_IN_BYTES);
  deplog_set = new std::unordered_set<slamp::KEY, slamp::KEYHash, slamp::KEYEqual>();
  deplog_vec = new std::vector<slamp::KEY>();
  deplog_vec->reserve(DEPLOG_VEC_SIZE);

  smmap->init_stack(SIZE_8M, pid);
}

void fini(const char *filename) {
  convertVectorToSet();
  std::ofstream of(filename);
  of << target_loop_id << " " << 0 << " " << 0 << " "
       << 0 << " " << 0 << " " << 0 << "\n";

  std::set<slamp::KEY, slamp::KEYComp> ordered(deplog_set->begin(), deplog_set->end());
  for (auto &k: ordered) {
    of << target_loop_id << " " << k.src << " " << k.dst << " " << k.dst_bare << " "
       << (k.cross ? 1 : 0) << " " << 1 << " ";
    of << "\n";
  }

  delete smmap;
  delete deplog_set;
}

void allocate(void *addr, uint64_t size) {
  smmap->allocate(addr, size);
  // std::cout << "allocate " << addr << " " << size << std::endl;
}


// void log(TS ts, const uint32_t dst_inst, TS *pts, const uint32_t bare_inst,
         // uint64_t  addr, uint64_t  value, uint8_t  size) {
void log(TS ts, const uint32_t dst_inst, const uint32_t bare_inst){ 

    uint32_t src_inst = GET_INSTR(ts);
    uint64_t src_iter = GET_ITER(ts);

    // uint64_t src_invoc = GET_INVOC(ts);

    slamp::KEY key(src_inst, dst_inst, bare_inst, src_iter != slamp_iteration);
    
    // std::cout << "src_inst: " << src_inst << " dst_inst: " << dst_inst << " bare_inst: " << bare_inst << " src_iter: " << src_iter << " slamp_iteration: " << slamp_iteration << " src_iter != slamp_iteration: " << (src_iter != slamp_iteration) << std::endl;
    deplog_vec->emplace_back(key);
    deplog_vec_counter++;
    if (deplog_vec_counter == DEPLOG_VEC_SIZE - 1) {
      convertVectorToSet();
      deplog_vec->resize(0);
      deplog_vec_counter = 0;
    }
}

// template <unsigned size>
void load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  TS* s = (TS*)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS tss = s[0];
  if (tss != 0) {
    log(tss, instr, bare_instr);
  }
}

// template <unsigned size>
void store(uint32_t instr, uint32_t bare_instr, const uint64_t addr) {
  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  // if (!smmap->is_allocated(s)) {
  //   std::cout << "store not allocated: " << instr << " " << bare_instr << " " << addr << std::endl;
  // }
  TS ts = CREATE_TS(instr, slamp_iteration, slamp_invocation);

  // TODO: handle output dependence. ignore it as of now.
  // if (ASSUME_ONE_ADDR) {
  s[0] = ts;
  // } else {
    // for (auto i = 0; i < size; i++)
      // s[i] = ts;
  // }
}

void loop_invoc() {
  slamp_iteration = 0;
  slamp_invocation++;
}

void loop_iter() {
  slamp_iteration++;
}
} // namespace DepMod
