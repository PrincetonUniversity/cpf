#include <boost/interprocess/interprocess_fwd.hpp>
#include <cstdint>
#include "ProfilingModules/DependenceModule.h"
#include <iostream>
#include <sstream>
#include "sw_queue_astream.h"


#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

namespace bip = boost::interprocess;

#define DEBUG 0
#define ACTION 1

// create segment and corresponding allocator
bip::fixed_managed_shared_memory *segment, *segment2;
static SW_Queue the_queue;

#define CONSUME         sq_consume(the_queue);
#define PRODUCE(x)      sq_produce(the_queue,(uint64_t)x);

#define PRODUCE_2(x,y)  PRODUCE( (((uint64_t)x)<<32) | (uint32_t)(y) )
#define CONSUME_2(x,y)  do { uint64_t tmp = CONSUME; x = (uint32_t)(tmp>>32); y = (uint32_t) tmp; } while(0)

int main() {
  segment = new bip::fixed_managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *2, (void*)(1UL << 32));
  segment2 = new bip::fixed_managed_shared_memory(bip::open_or_create, "MySharedMemory2", sizeof(uint64_t) *QSIZE *2, (void*)(1UL << 28));
    // managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *2);
  // auto a_queue = new atomic_queue::AtomicQueueB<shm::Element, shm::char_alloc, shm::NIL>(65536);
  the_queue = static_cast<SW_Queue>(segment->find_or_construct<sw_queue_t>("MyQueue")());
  auto data = static_cast<uint64_t*>(segment2->find_or_construct<uint64_t>("smtx_queue_data")[QSIZE]());
  // segment = new bip::managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *2);
  // auto a_queue = new atomic_queue::AtomicQueueB<shm::Element, shm::char_alloc, shm::NIL>(65536);
  // the_queue = static_cast<SW_Queue>(segment->find_or_construct<sw_queue_t>("MyQueue")());
  // auto data = static_cast<uint64_t*>(segment->find_or_construct<uint64_t>("smtx_queue_data")[QSIZE]());
  if (the_queue == nullptr) {
    std::cout << "Error: could not create queue" << std::endl;
    return 1;
  }
  the_queue->data = data;
  if (the_queue->data == nullptr) {
    std::cout << "Error: could not create queue data" << std::endl;
    return 1;
  }
  /* Initialize the queue data structure */
  the_queue->p_data = (uint64_t) the_queue->data;
  the_queue->c_inx = 0;
  the_queue->c_margin = 0;
  the_queue->p_glb_inx = 0;
  the_queue->c_glb_inx = 0;
  the_queue->ptr_c_glb_inx = &(the_queue->c_glb_inx);
  the_queue->ptr_p_glb_inx = &(the_queue->p_glb_inx);


  uint64_t counter = 0;
  // shm::shared_string v(char_alloc);

  using Action = DepMod::DepModAction;

  uint32_t loop_id;
  while (true) {
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
      instr = (uint32_t)CONSUME;
      addr = CONSUME;
      bare_instr = (uint32_t)CONSUME;
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
      instr = (uint32_t)CONSUME;
      bare_instr = (uint32_t)CONSUME;
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
      std::cout << "Unknown action: " << v << std::endl;
      exit(-1);
    }

    if (counter % 100000000 == 0) {
      std::cout << "Processed " << counter / 1000000 << "M events" << std::endl;
    }
  }
}
