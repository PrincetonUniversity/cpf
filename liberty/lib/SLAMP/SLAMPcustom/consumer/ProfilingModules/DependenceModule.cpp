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
std::mutex mutex_process_load_store;

constexpr unsigned LOCALWRITE_THREADS = 1;
std::mutex localwrite_mutexes[LOCALWRITE_THREADS];

#define SIZE_8M  0x800000

#define DEPLOG_VEC_SIZE 1'000'000

static slamp::MemoryMap* smmap = nullptr;
static std::unordered_set<slamp::KEY, slamp::KEYHash, slamp::KEYEqual> *deplog_set;
static std::vector<slamp::KEY> *deplog_vec[LOCALWRITE_THREADS];
static uint64_t slamp_iteration = 0;
static uint64_t slamp_invocation = 0;
static uint32_t target_loop_id = 0;

struct LoadStoreEvent {
  // bool isLoad;
  uint64_t addr;
  // uint64_t value;
  TS ts;
  // uint32_t instr;
  // uint32_t bare_instr;
  // uint64_t invocation;
  // uint64_t iteration;
};
// constexpr unsigned MAX_EVENTS = 100'000'000;
constexpr unsigned MAX_EVENTS = 100'000;
static std::vector<LoadStoreEvent> *loadstore_vec, *loadstore_vec0, *loadstore_vec1;
static uint64_t loadstore_vec_counter = 0;

static std::chrono::duration<long, std::micro> total_time_off_critical_path(0);
static std::chrono::duration<long, std::micro> loadstore_vec_time(0);

static void convertVectorToSet(const unsigned thread_id) {

  // launch 56 threads to convert the vector to set independently, chunking
  constexpr auto THREADS = 56;
  std::thread t[THREADS];
  for (unsigned long i = 0; i < THREADS; i++) {
    t[i] = std::thread(
        [&](int id) {
          // take the chunk and convert to a set and return
          auto *deplog_set_chunk =
              new std::unordered_set<slamp::KEY, slamp::KEYHash,
                                     slamp::KEYEqual>();
          deplog_set_chunk->reserve(DEPLOG_VEC_SIZE / THREADS + 1);
          auto begin = id * (DEPLOG_VEC_SIZE / THREADS);
          auto end = (id + 1) * (DEPLOG_VEC_SIZE / THREADS);
          deplog_set_chunk->insert(deplog_vec[thread_id]->begin() + begin,
                                   deplog_vec[thread_id]->begin() + end);

          m.lock();
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

  // std::cout << "Merging vec to set, set length " << deplog_set->size()
            // << std::endl;
}


namespace DepMod {
// init: setup the shadow memory
void init(uint32_t loop_id, uint32_t pid) {

  target_loop_id = loop_id;
  smmap = new slamp::MemoryMap(TIMESTAMP_SIZE_IN_BYTES);
  deplog_set = new std::unordered_set<slamp::KEY, slamp::KEYHash, slamp::KEYEqual>();
  for (auto & i : deplog_vec) {
    i = new std::vector<slamp::KEY>();
    i->reserve(DEPLOG_VEC_SIZE);
  }

  loadstore_vec0 = new std::vector<LoadStoreEvent>();
  loadstore_vec1 = new std::vector<LoadStoreEvent>();
  loadstore_vec = loadstore_vec0; // double buffering
  loadstore_vec0->reserve(MAX_EVENTS);
  loadstore_vec1->reserve(MAX_EVENTS);

  smmap->init_stack(SIZE_8M, pid);
}

void handleLoadAndStore(std::vector<LoadStoreEvent> *loadstore_vec);

void fini(const char *filename) {
  // show in seconds
  std::cout << "Total time off critical path: "
            << total_time_off_critical_path.count() / 1000000.0 << "s"
            << std::endl;

  std::cout << "Loadstore vec time: "
    << loadstore_vec_time.count() / 1000000.0 << "s"
    << std::endl;

  mutex_process_load_store.lock();
  handleLoadAndStore(loadstore_vec);
  for (int i = 0; i < LOCALWRITE_THREADS; i++) {
    convertVectorToSet(i);
  }
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
void log(const unsigned thread_id, TS ts, const uint32_t dst_inst, const uint32_t bare_inst, const uint64_t load_invocation, const uint64_t load_iteration){ 

    uint32_t src_inst = GET_INSTR(ts);

    uint64_t src_invoc = GET_INVOC(ts);
    uint64_t src_iter = GET_ITER(ts);

    if (src_invoc != GET_INVOC(load_invocation)) {
      return;
    }

    // uint64_t src_invoc = GET_INVOC(ts);

    slamp::KEY key(src_inst, dst_inst, bare_inst, src_iter != load_iteration);
    
    // std::cout << "src_inst: " << src_inst << " dst_inst: " << dst_inst << " bare_inst: " << bare_inst << " src_iter: " << src_iter << " slamp_iteration: " << slamp_iteration << " src_iter != slamp_iteration: " << (src_iter != slamp_iteration) << std::endl;
    deplog_vec[thread_id]->emplace_back(key);
    if (deplog_vec[thread_id]->size() == DEPLOG_VEC_SIZE - 1) {
      convertVectorToSet(thread_id);
      deplog_vec[thread_id]->resize(0);
    }
}

void handleLoadAndStore(std::vector<LoadStoreEvent> *loadstore_vec) {

  // put in LOCALWRITE_THREADS threads to handle the loadstore_vec
  std::thread t[LOCALWRITE_THREADS];

  for (auto i = 0; i < LOCALWRITE_THREADS; i++) {
    unsigned begin = i * (loadstore_vec_counter / LOCALWRITE_THREADS);
    unsigned end = (i + 1) * (loadstore_vec_counter / LOCALWRITE_THREADS);
    unsigned thread_id = 0;
    // t[i] = std::thread(
        // [&](const unsigned thread_id, const unsigned begin, const unsigned end) {
          for (auto i = begin; i < end; i++) {
            auto &e = (*loadstore_vec)[i];
            const auto &addr = e.addr;
            const auto &ts = e.ts;
            const auto invoc = GET_INVOC(ts);
            const auto iter = GET_ITER(ts);
            const auto instr = GET_INSTR(ts);

            TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
            // if (e.isLoad) {
            // load
            // std::cout << "load " << e.addr << " " << e.value << " " <<
            // e.instr
            // << " " << e.bare_instr << std::endl; smmap->load(e.addr,
            // e.value, e.instr, e.bare_instr);

            // std::cout << "load " << instr << " " << addr << " " <<
            // bare_instr
            // << " "
            // << value << std::endl;
            TS tss = s[0];
            if (tss != 0) {
              log(thread_id, tss, instr, instr, invoc, iter);
            }
            // }
            // else {
            // // store
            // // std::cout << "store " << e.addr << " " << e.value << " " <<
            // // e.instr
            // // << " " << e.bare_instr << std::endl; smmap->store(e.addr,
            // // e.value, e.instr, e.bare_instr);
            // // if (!smmap->is_allocated(s)) {
            // //   std::cout << "store not allocated: " << instr << " " <<
            // //   bare_instr << " " << addr << std::endl;
            // // }
            // // TODO: handle output dependence. ignore it as of now.
            // // if (ASSUME_ONE_ADDR) {
            // s[0] = ts;
            // // } else {
            // // for (auto i = 0; i < size; i++)
            // // s[i] = ts;
            // // }
            // }
          }
        // }, i, begin, end);
  }

  // for (auto & i : t) {
    // i.join();
  // }
  // mutex_process_load_store.unlock();
}


// template <unsigned size>
void load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value) {

  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  // if (e.isLoad) {
  // load
  // std::cout << "load " << e.addr << " " << e.value << " " <<
  // e.instr
  // << " " << e.bare_instr << std::endl; smmap->load(e.addr,
  // e.value, e.instr, e.bare_instr);

  // std::cout << "load " << instr << " " << addr << " " <<
  // bare_instr
  // << " "
  // << value << std::endl;
  TS tss = s[0];
  if (tss != 0) {
    log(0, tss, instr, instr, slamp_invocation, slamp_iteration);
  }
  // // auto start = std::chrono::high_resolution_clock::now();
  // loadstore_vec->emplace_back(LoadStoreEvent{addr, CREATE_TS(instr, slamp_iteration, slamp_invocation)});
  // loadstore_vec_counter++;
  // // auto end = std::chrono::high_resolution_clock::now();
  // // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // // loadstore_vec_time += duration;
  // if (loadstore_vec_counter == MAX_EVENTS - 1) {
  //   handleLoadAndStore(loadstore_vec);

  //   // // measure time in here
  //   // auto start = std::chrono::high_resolution_clock::now();

  //   // // run this asynchrously
  //   // mutex_process_load_store.lock();
  //   // std::thread t = std::thread(handleLoadAndStore, loadstore_vec);
  //   // t.detach();

  //   // // swap double buffer
  //   // loadstore_vec = loadstore_vec == loadstore_vec0 ? loadstore_vec1 : loadstore_vec0;
    
  //   // loadstore_vec->resize(0);
  //   // loadstore_vec_counter = 0;

  //   // auto end = std::chrono::high_resolution_clock::now();
  //   // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  //   // total_time_off_critical_path += duration;
  // }

}

// template <unsigned size>
void store(uint32_t instr, uint32_t bare_instr, const uint64_t addr) {
  // if (loadstore_vec_counter > 0) {
  //   handleLoadAndStore(loadstore_vec);
  //   loadstore_vec->resize(0);
  //   loadstore_vec_counter = 0;
  // }

  TS ts = CREATE_TS(instr, slamp_iteration, slamp_invocation);
  TS *shadow_addr = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  shadow_addr[0] = ts;

  // // auto start = std::chrono::high_resolution_clock::now();
  // loadstore_vec->emplace_back(LoadStoreEvent{false, addr, CREATE_TS(instr, slamp_iteration, slamp_invocation)});
  // loadstore_vec_counter++;
  // // auto end = std::chrono::high_resolution_clock::now();
  // // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // // loadstore_vec_time += duration;
  // if (loadstore_vec_counter == MAX_EVENTS - 1) {
  //   // measure time in here
  //   auto start = std::chrono::high_resolution_clock::now();
  //   mutex_process_load_store.lock();
  //   std::thread t = std::thread(handleLoadAndStore, loadstore_vec);
  //   t.detach();
  //   loadstore_vec = loadstore_vec == loadstore_vec0 ? loadstore_vec1 : loadstore_vec0;

  //   loadstore_vec->resize(0);
  //   loadstore_vec_counter = 0;
  //   auto end = std::chrono::high_resolution_clock::now();
  //   auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  //   total_time_off_critical_path += duration;
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
