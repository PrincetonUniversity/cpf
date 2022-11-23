#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <xmmintrin.h>

#include "ProfilingModules/DependenceModule.h"
#include "ProfilingModules/PointsToModule.h"
#include "sw_queue_astream.h"

#define ATTRIBUTE(x) __attribute__((x))
namespace bip = boost::interprocess;

static inline uint64_t rdtsc() {
  uint64_t a, d;
  __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
  return (d << 32) | a;
}

#define DEBUG 0
#define ACTION 1
#define MEASURE_TIME 0

// #define CONSUME         sq_consume(the_queue);

static uint64_t load_time(0);
static uint64_t store_time(0);
static uint64_t alloc_time(0);

// create segment and corresponding allocator
bip::fixed_managed_shared_memory *segment;

void consume_loop_pt(DoubleQueue &dq, PointsToModule &ptMod) ATTRIBUTE(noinline) {
  uint64_t rdtsc_start = 0;
  uint64_t counter = 0;
  uint32_t loop_id;

  using Action = PointsToModAction;
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

  bool finished = false;
  while (true) {
    dq.check();
    uint32_t v;
    v = dq.consumePacket();
    counter++;

    // convert v to action
    auto action = static_cast<Action>(v);

    switch (action) {
    case Action::INIT: {
      uint32_t pid;
      // loop_id = (uint32_t)dq.consume();
      // pid = (uint32_t)dq.consume();
      dq.unpack_32_32(loop_id, pid);
      rdtsc_start = rdtsc();

      if (DEBUG) {
        std::cout << "INIT: " << loop_id << " " << pid << std::endl;
      }
      if (ACTION) {
        ptMod.init(loop_id, pid);
      }
      break;
    };
    case Action::ALLOC: {
      uint32_t instr;
      uint64_t addr;
      uint32_t size;
      dq.unpack_24_32_64(instr, size, addr);

      if (DEBUG) {
        std::cout << "ALLOC: " << addr << " " << size << std::endl;
      }
      if (ACTION) {
        measure_time(alloc_time, [&]() {
          ptMod.allocate(reinterpret_cast<void *>(addr), instr, size);
        });
      }
      break;
    };
    case Action::FREE: {
      uint64_t addr;
      dq.unpack_64(addr);

      if (DEBUG) {
        std::cout << "FREE: " << addr << std::endl;
      }
      if (ACTION) {
        measure_time(alloc_time,
            [&]() { ptMod.free(reinterpret_cast<void *>(addr)); });
      }
      break;
    };
    case Action::LOOP_INVOC: {
      if (DEBUG) {
        std::cout << "LOOP_INVOC" << std::endl;
      }

      if (ACTION) {
        ptMod.loop_invoc();
      }
      break;
    };
    case Action::LOOP_ITER: {
      if (DEBUG) {
        std::cout << "LOOP_ITER" << std::endl;
      }
      if (ACTION) {
        ptMod.loop_iter();
      }
      break;
    };
    case Action::LOOP_ENTRY: {
      uint32_t loop_id;
      dq.unpack_32(loop_id);
      if (DEBUG) {
        std::cout << "LOOP_ENTRY: " << loop_id << std::endl;
      }
      if (ACTION) {
        ptMod.loop_entry(loop_id);
      }
      break;
    };
    case Action::LOOP_EXIT: {
      uint32_t loop_id;
      dq.unpack_32(loop_id);
      if (DEBUG) {
        std::cout << "LOOP_EXIT: " << loop_id << std::endl;
      }
      if (ACTION) {
        ptMod.loop_exit(loop_id);
      }
      break;
    };
    case Action::FUNC_ENTRY: {
      uint32_t func_id;
      dq.unpack_32(func_id);
      if (DEBUG) {
        std::cout << "FUNC_ENTRY: " << func_id << std::endl;
      }
      if (ACTION) {
        ptMod.func_entry(func_id);
      }
      break;
    };
    case Action::FUNC_EXIT: {
      uint32_t func_id;
      dq.unpack_32(func_id);
      if (DEBUG) {
        std::cout << "FUNC_EXIT: " << func_id << std::endl;
      }
      if (ACTION) {
        ptMod.func_exit(func_id);
      }
      break;
    };

    case Action::POINTS_TO_ARG: {
      uint32_t fcnId;
      uint32_t argId;
      uint64_t addr;
      dq.unpack_32_64(fcnId, addr);
      // break fcnId into argId and fcnId, 16 bit each
      argId = fcnId & 0xFFFF;
      fcnId = fcnId >> 16;
      if (DEBUG) {
        std::cout << "POINTS_TO_ARG: " << fcnId << " " << argId << " " << addr
                  << std::endl;
      }

      if (ACTION) {
        ptMod.points_to_arg(fcnId, argId, reinterpret_cast<void *>(addr));
      }
      break;
    };

    case Action::POINTS_TO_INST: {
      uint32_t instId;
      uint64_t addr;
      dq.unpack_32_64(instId, addr);
      if (DEBUG) {
        std::cout << "POINTS_TO_INST: " << instId << " " << addr << std::endl;
      }
      if (ACTION) {
        ptMod.points_to_inst(instId, reinterpret_cast<void *>(addr));
      }
      break;
    };

    case Action::FINISHED: {
      uint64_t rdtsc_end = rdtsc();
      // total cycles
      uint64_t total_cycles = rdtsc_end - rdtsc_start;
      std::cout << "Finished loop: " << loop_id << " after " << counter
                << " events" << std::endl;
      // print time in seconds
      std::cout << "Total time: " << total_cycles / 2.6e9 << " s" << std::endl;
      if (MEASURE_TIME) {
        std::cout << "Load time: " << load_time / 2.6e9 << " s" << std::endl;
        std::cout << "Store time: " << store_time / 2.6e9 << " s" << std::endl;
        std::cout << "Alloc time: " << alloc_time / 2.6e9 << " s" << std::endl;
      }
      finished = true;

      if (ACTION) {
        ptMod.fini("ptlog.txt");
      }

      break;
    };
    default:
      std::cout << "Unknown action: " << (uint64_t)v << std::endl;

      std::cout << "Is ready to read?:" << dq.qNow->ready_to_read << " "
                << "Is ready to write?:" << dq.qNow->ready_to_write << std::endl;
      std::cout << "Index: " << dq.index << " Size:" << dq.qNow->size << std::endl;

      for (int i = 0; i < 101; i++) {
        std::cout << dq.qNow->data[dq.index - 100 + i] << " ";
      }
      exit(-1);
    }

    // if (counter % 100'000'000 == 0) {
      // std::cout << "Processed " << counter / 1'000'000 << "M events" << std::endl;
    // }
    if (finished) {
      break;
    }
  }
}

void consume_loop(DoubleQueue &dq, DependenceModule &depMod) ATTRIBUTE(noinline) {
  uint64_t rdtsc_start = 0;
  uint64_t counter = 0;
  uint32_t loop_id;

  using Action = DepModAction;
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

  bool finished = false;
  while (true) {
    dq.check();
    uint32_t v;
    v = dq.consumePacket();
    counter++;

    switch (v) {
    case Action::INIT: {
      uint32_t pid;
      // loop_id = (uint32_t)dq.consume();
      // pid = (uint32_t)dq.consume();
      dq.unpack_32_32(loop_id, pid);
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
      // uint32_t bare_instr;
      uint64_t value = 0;

      dq.unpack_32_64(instr, addr);

      if (DEBUG) {
        std::cout << "LOAD: " << instr << " " << addr // << " " << bare_instr
                  << " " << value << std::endl;
      }
      if (ACTION) {
        measure_time(load_time,
                     [&]() { depMod.load(instr, addr, instr, value); });
                     // [&]() { depMod.load(instr, addr, bare_instr, value); });
      }

      break;
    };
    case Action::STORE: {
      uint32_t instr;
      // uint32_t bare_instr;
      uint64_t addr;
      dq.unpack_32_64(instr, addr);

      if (DEBUG) {
        std::cout << "STORE: " << instr << " " << addr // << " " << bare_instr
                  << std::endl;
      }
      if (ACTION) {
        measure_time(store_time,
                     [&]() { depMod.store(instr, instr, addr); });
                     // [&]() { depMod.store(instr, bare_instr, addr); });
      }
      break;
    };
    case Action::ALLOC: {
      uint64_t addr;
      uint32_t size;
      dq.unpack_32_64(size, addr);

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

      // if (ACTION) {
      //   std::stringstream ss;
      //   ss << "deplog-" << loop_id << ".txt";
      //   depMod.fini(ss.str().c_str());
      // }

      uint64_t rdtsc_end = rdtsc();
      // total cycles
      uint64_t total_cycles = rdtsc_end - rdtsc_start;
      std::cout << "Finished loop: " << loop_id << " after " << counter
                << " events" << std::endl;
      // print time in seconds
      std::cout << "Total time: " << total_cycles / 2.6e9 << " s" << std::endl;
      if (MEASURE_TIME) {
        std::cout << "Load time: " << load_time / 2.6e9 << " s" << std::endl;
        std::cout << "Store time: " << store_time / 2.6e9 << " s" << std::endl;
        std::cout << "Alloc time: " << alloc_time / 2.6e9 << " s" << std::endl;
      }
      finished = true;

      break;
    };
    default:
      std::cout << "Unknown action: " << (uint64_t)v << std::endl;

      std::cout << "Is ready to read?:" << dq.qNow->ready_to_read << " "
                << "Is ready to write?:" << dq.qNow->ready_to_write << std::endl;
      std::cout << "Index: " << dq.index << " Size:" << dq.qNow->size << std::endl;

      for (int i = 0; i < 101; i++) {
        std::cout << dq.qNow->data[dq.index - 100 + i] << " ";
      }
      exit(-1);
    }

    // if (counter % 100'000'000 == 0) {
      // std::cout << "Processed " << counter / 1'000'000 << "M events" << std::endl;
    // }
    if (finished) {
      break;
    }
  }
}

int main(int argc, char** argv) {
  segment = new bip::fixed_managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint32_t) *QSIZE *4, (void*)(1UL << 32));

  Queue_p dqA, dqB;
  // double buffering
  dqA = segment->construct<Queue>("DQ_A")(Queue());
  dqB = segment->construct<Queue>("DQ_B")(Queue());
  auto dataA = segment->construct<uint32_t>("DQ_Data_A")[QSIZE]();
  auto dataB = segment->construct<uint32_t>("DQ_Data_B")[QSIZE]();

  // find the first 16byte alignment 
  dataA = (uint32_t*)(((uint64_t)dataA + 15) & ~15);
  dataB = (uint32_t*)(((uint64_t)dataB + 15) & ~15);
  dqA->init(dataA);
  dqB->init(dataB);


  // set the thread count
  constexpr unsigned THREAD_COUNT = 1;
  constexpr unsigned MASK = THREAD_COUNT - 1;

  unsigned running_threads= THREAD_COUNT;
  std::mutex m;
  std::condition_variable cv;

  DoubleQueue dq(dqA, dqB, true, running_threads, m, cv);
  PointsToModule ptMod(0, 0);
  consume_loop_pt(dq, ptMod);

  // std::vector<std::thread> threads;
  // DoubleQueue *dqs[THREAD_COUNT];
  // DependenceModule *depMods[THREAD_COUNT];

  // for (unsigned i = 0; i < THREAD_COUNT; i++) {
  //   dqs[i] = new DoubleQueue(dqA, dqB, true, running_threads, m, cv);
  //   depMods[i] = new DependenceModule(MASK, i);
  // }

  // if (THREAD_COUNT == 1) {
  //   std::cout << "Running in main thread" << std::endl;
  //   // single threaded, easy to debug
  //   consume_loop(*dqs[0], *depMods[0]);

  //   depMods[0]->fini("deplog.txt");
  // } else {
  //   std::cout << "Running in " << THREAD_COUNT << " threads" << std::endl;
  //   for (unsigned i = 0; i < THREAD_COUNT; i++) {
  //     threads.emplace_back(std::thread([&](unsigned id) {
  //           consume_loop(*dqs[id], *depMods[id]);
  //           }, i));
  //   }

  //   for (auto &t : threads) {
  //     t.join();
  //   }

  //   for (unsigned i = 0; i < THREAD_COUNT; i++) {
  //     if (i != 0) {
  //       depMods[0]->merge_dep(*depMods[i]);
  //     }
  //   }

  //   depMods[0]->fini("deplog.txt");
  // }
}
