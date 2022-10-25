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

#define DEBUG 0
#define ACTION 1

// create segment and corresponding allocator
bip::fixed_managed_shared_memory *segment, *segment2;
static SW_Queue the_queue;
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

// #define CONSUME         sq_consume(the_queue);
#define CONSUME         consume();
#define PRODUCE(x)      sq_produce(the_queue,(uint64_t)x);

#define PRODUCE_2(x,y)  PRODUCE( (((uint64_t)x)<<32) | (uint32_t)(y) )
#define CONSUME_2(x,y)  do { uint64_t tmp = CONSUME; x = (uint32_t)(tmp>>32); y = (uint32_t) tmp; } while(0)

int main(int argc, char** argv) {

  segment = new bip::fixed_managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *4, (void*)(1UL << 32));
  // segment2 = new bip::fixed_managed_shared_memory(bip::open_or_create, "MySharedMemory2", sizeof(uint64_t) *QSIZE *2, (void*)(1UL << 28));
    // managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *2);
  // auto a_queue = new atomic_queue::AtomicQueueB<shm::Element, shm::char_alloc, shm::NIL>(65536);

  dqA = segment->construct<double_queue_t>("DQ_A")(double_queue_t());
  dqB = segment->construct<double_queue_t>("DQ_B")(double_queue_t());
  auto dataA = segment->construct<uint64_t>("DQ_Data_A")[QSIZE](uint64_t());
  auto dataB = segment->construct<uint64_t>("DQ_Data_B")[QSIZE](uint64_t());
  dqA->init(dataA);
  dqB->init(dataB);
  dq = dqB;
  dq_other = dqA;
  dq_data = dq->data;


  // the_queue = static_cast<SW_Queue>(segment->find_or_construct<sw_queue_t>("MyQueue")());
  // auto data = static_cast<uint64_t*>(segment2->find_or_construct<uint64_t>("smtx_queue_data")[QSIZE]());
  // segment = new bip::managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *2);
  // auto a_queue = new atomic_queue::AtomicQueueB<shm::Element, shm::char_alloc, shm::NIL>(65536);
  // the_queue = static_cast<SW_Queue>(segment->find_or_construct<sw_queue_t>("MyQueue")());
  // auto data = static_cast<uint64_t*>(segment->find_or_construct<uint64_t>("smtx_queue_data")[QSIZE]());

  // // measure time
  // auto start = std::chrono::high_resolution_clock::now();
  // unsigned long ROUND = 100;
  // // parse the ROUND number from cmd
  // if (argc > 1) {
  //   std::stringstream ss;
  //   ss << argv[1];
  //   ss >> ROUND;
  // }
  // for (int round = 0; round < ROUND; round++) {
  //   for (unsigned long i = 0; i < QSIZE; i++) {
  //     data[i] = i;
  //   }
  // }
  // auto end = std::chrono::high_resolution_clock::now();
  // std::chrono::duration<double> diff = end-start;
  // std::cout << "Time for " << ROUND * QSIZE / 1'000'000 << "M events : " << diff.count() << " s" << std::endl;
  // // throughput
  // std::cout << "Write Throughput: " << ROUND * QSIZE / diff.count() / 1'000'000 << "M events/s" << std::endl;

  // start = std::chrono::high_resolution_clock::now();
  // unsigned total = 0;
  // for (int round = 0; round < ROUND; round++) {
  //   for (unsigned long i = 0; i < QSIZE; i++) {
  //     total += data[i];
  //   }
  // }
  // end = std::chrono::high_resolution_clock::now();
  // diff = end-start;
  // std::cout << "Total = " << total << std::endl;
  // std::cout << "Time for " << ROUND * QSIZE / 1'000'000 << "M events : " << diff.count() << " s" << std::endl;
  // // throughput
  // std::cout << "Read Throughput: " << ROUND * QSIZE / diff.count() / 1'000'000 << "M events/s" << std::endl;


  // if (the_queue == nullptr) {
    // std::cout << "Error: could not create queue" << std::endl;
    // return 1;
  // }
  // the_queue->data = data;
  // if (the_queue->data == nullptr) {
    // std::cout << "Error: could not create queue data" << std::endl;
    // return 1;
  // }
  // [> Initialize the queue data structure <]
  // the_queue->p_data = (uint64_t) the_queue->data;
  // the_queue->c_inx = 0;
  // the_queue->c_margin = 0;
  // the_queue->p_glb_inx = 0;
  // the_queue->c_glb_inx = 0;
  // the_queue->ptr_c_glb_inx = &(the_queue->c_glb_inx);
  // the_queue->ptr_p_glb_inx = &(the_queue->p_glb_inx);


  uint64_t counter = 0;
  // shm::shared_string v(char_alloc);

  using Action = DepMod::DepModAction;

  uint32_t loop_id;
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

      if (DEBUG) {
        std::cout << "INIT: " << loop_id << " " << pid << std::endl;
      }
#if ACTION
      DepMod::init(loop_id, pid);
#endif
      break;
    };
    case Action::LOAD: {
      uint32_t instr;
      uint64_t addr;
      uint32_t bare_instr;
      uint64_t value = 0;
      CONSUME_2(instr, bare_instr);
      addr = CONSUME;
      // value = CONSUME;
      if (DEBUG) {
        std::cout << "LOAD: " << instr << " " << addr << " " << bare_instr
                  << " " << value << std::endl;
      }
#if ACTION
      DepMod::load(instr, addr, bare_instr, value);
      // DepMod::load(instr, addr, bare_instr, value);
#endif

      break;
    };
    case Action::STORE: {
      uint32_t instr;
      uint32_t bare_instr;
      uint64_t addr;
      CONSUME_2(instr, bare_instr);
      addr = CONSUME;
      if (DEBUG) {
        std::cout << "STORE: " << instr << " " << bare_instr << " " << addr
                  << std::endl;
      }
#if ACTION
      DepMod::store(instr, bare_instr, addr);
#endif
      break;
    };
    case Action::ALLOC: {
      uint64_t addr;
      uint64_t size;
      addr = CONSUME;
      size = CONSUME;
      if (DEBUG) {
        std::cout << "ALLOC: " << addr << " " << size << std::endl;
      }
#if ACTION
      DepMod::allocate(reinterpret_cast<void *>(addr), size);
#endif
      break;
    };
    case Action::LOOP_INVOC: {
#if ACTION
      DepMod::loop_invoc();
#endif

      if (DEBUG) {
        std::cout << "LOOP_INVOC" << std::endl;
      }
      break;
    };
    case Action::LOOP_ITER: {
#if ACTION
      DepMod::loop_iter();
#endif

      if (DEBUG) {
        std::cout << "LOOP_ITER" << std::endl;
      }
      break;
    };
    case Action::FINISHED: {
      std::stringstream ss;
      ss << "deplog-" << loop_id << ".txt";
#if ACTION
      DepMod::fini(ss.str().c_str());
#endif
      std::cout << "Finished loop: " << loop_id << " after " << counter
                << " events" << std::endl;
      break;
    };
    default:
      std::cout << "Unknown action: " << (uint64_t)v << std::endl;

      std::cout << "Is ready to read?:" << dq->ready_to_read << " " << "Is ready to write?:" << dq->ready_to_write << std::endl;
      std::cout << "Index: " << dq_index << " Size:" << dq->size << std::endl;
      for (int i = 0; i < 101; i++) {
        std::cout << dq->data[dq_index - 100 + i] << " ";
      }
      exit(-1);
    }

    if (counter % 100000000 == 0) {
      std::cout << "Processed " << counter / 1000000 << "M events" << std::endl;
    }
  }
}
