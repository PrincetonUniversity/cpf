#include <boost/interprocess/interprocess_fwd.hpp>
#include <cstdint>
#include "ProfilingModules/DependenceModule.h"
#include <iostream>
#include <sstream>
#include "sw_queue_astream.h"
#include <chrono>


#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <xmmintrin.h>

namespace bip = boost::interprocess;
using Action = DepModAction;

static inline uint64_t rdtsc() {
  uint64_t a, d;
  __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
  return (d << 32) | a;
}

#define DEBUG 0
#define ACTION 1
#define MEASURE_TIME 1

// #define CONSUME         sq_consume(the_queue);
#define CONSUME         consume();

static uint64_t load_time(0);
static uint64_t store_time(0);
static uint64_t alloc_time(0);

// create segment and corresponding allocator
bip::fixed_managed_shared_memory *segment;
static double_queue_p dqA, dqB, dq, dq_other;
static uint64_t dq_index = 0;
static uint64_t dq_size = 0;
static uint64_t *dq_data;

static void swap(){
  if(dq == dqA){
    dq = dqB;
    dq_other = dqA;
  }else{
    dq = dqA;
    dq_other = dqB;
  }
  dq_data = dq->data;
}

static void check() {
  if (dq_index == dq_size){
    dq->ready_to_read = false;
    dq->ready_to_write = true;
    while (!dq_other->ready_to_read){
      // spin
      usleep(10);
    }
    swap();
    dq->ready_to_write = false;
    dq_index = 0;
    dq_size = dq->size;
  }
}

static inline uint64_t consume(){
  uint64_t ret = dq_data[dq_index];
  dq_index++;
  // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_NTA);
  return ret;
}

static inline void consume_64_64(uint64_t &x, uint64_t &y){
  x = dq_data[dq_index];
  dq_index++;
  y = dq_data[dq_index];
  dq_index++;
  _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
  // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_NTA);
}

static inline void consume_32_32_64(uint32_t &x, uint32_t &y, uint64_t &z){
  uint64_t tmp = dq_data[dq_index];
  dq_index++;
  x = (tmp >> 32) & 0xFFFFFFFF;
  y = tmp & 0xFFFFFFFF;
  z = dq_data[dq_index];
  dq_index++;
  _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
  // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_NTA);
}

void consume_loop() {

  DependenceModule depMod(0x3, 1);

  uint64_t rdtsc_start = 0;
  uint64_t counter = 0;
  uint32_t loop_id;

  // measure time with lambda action
  auto measure_time = [](uint64_t &time, auto action) {
    // measure time with rdtsc
    if (MEASURE_TIME) {
      uint64_t start = rdtsc();
      action();
      uint64_t end = rdtsc();
      time += end - start;
    }
    else {
      action();
    }
  };

  while (true) {
    check();
    char v;
    v = (char)CONSUME;
    counter++;

    switch (v) {
    case Action::INIT: {
      uint32_t pid;
      loop_id = (uint32_t)CONSUME;
      pid = (uint32_t)CONSUME;
      rdtsc_start = rdtsc();

      if (DEBUG) {
        std::cout << "INIT: " << loop_id << " " << pid << std::endl;
      }
      if (ACTION) {
        depMod.init(loop_id, pid);
      }
      break;
    };
    case Action::LOAD: {
      uint32_t instr;
      uint64_t addr;
      uint32_t bare_instr;
      uint64_t value = 0;

      consume_32_32_64(instr, bare_instr, addr);

      if (DEBUG) {
        std::cout << "LOAD: " << instr << " " << addr << " " << bare_instr
                  << " " << value << std::endl;
      }
      if (ACTION) {
        measure_time(load_time,
                     [&]() { depMod.load(instr, addr, bare_instr, value); });
      }

      break;
    };
    case Action::STORE: {
      uint32_t instr;
      uint32_t bare_instr;
      uint64_t addr;
      consume_32_32_64(instr, bare_instr, addr);

      if (DEBUG) {
        std::cout << "STORE: " << instr << " " << bare_instr << " " << addr
                  << std::endl;
      }
      if (ACTION) {
        measure_time(store_time,
                     [&]() { depMod.store(instr, bare_instr, addr); });
      }
      break;
    };
    case Action::ALLOC: {
      uint64_t addr;
      uint64_t size;
      consume_64_64(addr, size);

      if (DEBUG) {
        std::cout << "ALLOC: " << addr << " " << size << std::endl;
      }
      if (ACTION) {
        measure_time(alloc_time, [&]() {
          depMod.allocate(reinterpret_cast<void *>(addr), size);
        });
      }
      break;
    };
    case Action::LOOP_INVOC: {
      if (DEBUG) {
        std::cout << "LOOP_INVOC" << std::endl;
      }

      if (ACTION) {
        depMod.loop_invoc();
      }
      break;
    };
    case Action::LOOP_ITER: {
      if (DEBUG) {
        std::cout << "LOOP_ITER" << std::endl;
      }
      if (ACTION) {
        depMod.loop_iter();
      }
      break;
    };
    case Action::FINISHED: {
      if (ACTION) {
        std::stringstream ss;
        ss << "deplog-" << loop_id << ".txt";
        depMod.fini(ss.str().c_str());
      }

      uint64_t rdtsc_end = rdtsc();
      // total cycles
      uint64_t total_cycles = rdtsc_end - rdtsc_start;
      std::cout << "Finished loop: " << loop_id << " after " << counter
                << " events" << std::endl;
      // print time in seconds
      std::cout << "Total time: " << total_cycles / 2.6e9 << " s" << std::endl;
      std::cout << "Load time: " << load_time / 2.6e9 << " s" << std::endl;
      std::cout << "Store time: " << store_time / 2.6e9 << " s" << std::endl;
      std::cout << "Alloc time: " << alloc_time / 2.6e9 << " s" << std::endl;
      exit(0);

      break;
    };
    default:
      std::cout << "Unknown action: " << (uint64_t)v << std::endl;

      std::cout << "Is ready to read?:" << dq->ready_to_read << " "
                << "Is ready to write?:" << dq->ready_to_write << std::endl;
      std::cout << "Index: " << dq_index << " Size:" << dq->size << std::endl;

      for (int i = 0; i < 101; i++) {
        std::cout << dq->data[dq_index - 100 + i] << " ";
      }
      exit(-1);
    }

    if (counter % 100'000'000 == 0) {
      std::cout << "Processed " << counter / 1'000'000 << "M events" << std::endl;
    }
  }

}

int main(int argc, char** argv) {
  segment = new bip::fixed_managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *4, (void*)(1UL << 32));

  // double buffering
  dqA = segment->construct<double_queue_t>("DQ_A")(double_queue_t());
  dqB = segment->construct<double_queue_t>("DQ_B")(double_queue_t());
  auto dataA = segment->construct<uint64_t>("DQ_Data_A")[QSIZE](uint64_t());
  auto dataB = segment->construct<uint64_t>("DQ_Data_B")[QSIZE](uint64_t());
  dqA->init(dataA);
  dqB->init(dataB);
  dq = dqB;
  dq_other = dqA;
  dq_data = dq->data;

  consume_loop();
}
