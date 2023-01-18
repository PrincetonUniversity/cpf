#include <boost/lockfree/spsc_queue.hpp> // ring buffer

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <cstdint>
#include "ProfilingModules/DependenceModule.h"
#include <iostream>
#include <sstream>

#define DEBUG 0
#define ACTION 0


namespace bip = boost::interprocess;
namespace shm
{
    // typedef bip::allocator<char, bip::managed_shared_memory::segment_manager> char_alloc;
    // typedef bip::basic_string<char, std::char_traits<char>, char_alloc >      shared_string;

    // shared_string, 
    using ring_buffer = boost::lockfree::spsc_queue<char, boost::lockfree::capacity<4194304>>;
}


// 2MB
#define LOCAL_BUFFER_SIZE 4194304

struct LocalReceiveBuffer {
  shm::ring_buffer *queue;

  char buffer[LOCAL_BUFFER_SIZE];
  unsigned counter = 0;
  unsigned buffer_size = 0;

  LocalReceiveBuffer(shm::ring_buffer *queue) : queue(queue) {}

  template <typename T>
  void pop(T &t) {
    size_t size = sizeof(T);
    T value = 0;
    uint8_t current_byte = 0;

    while (current_byte < size) {
      // load the value from the buffer
      while (counter < buffer_size && current_byte < size) {
        // lsb first
        // auto v = (uint64_t)(uint8_t)buffer[counter];
        // value |= (v << (8 * current_byte));
        counter++;
        current_byte++;
      }

      // load the buffer up
      if (counter == buffer_size) {
        buffer_size = queue->pop(buffer, LOCAL_BUFFER_SIZE);
        counter = 0;
      }
    }

    // std::cout << "pop " << (uint64_t)value << std::endl;

    // set the value
    t = value;
  }
};


int main()
{
    // create segment and corresponding allocator
    bip::managed_shared_memory segment(bip::open_or_create, "MySharedMemory", 104857600);
    // shm::char_alloc char_alloc(segment.get_segment_manager());

    shm::ring_buffer *queue = segment.find_or_construct<shm::ring_buffer>("queue")();
    LocalReceiveBuffer buffer(queue);

    uint64_t counter = 0;
    // shm::shared_string v(char_alloc);

    using Action=DepMod::DepModAction;

    uint32_t loop_id;
    // while (true) {
    while (false) {
      char v;
      buffer.pop(v);
      counter++;

      switch (v) {
      case Action::INIT: {
        uint32_t pid;
        buffer.pop(loop_id);
        buffer.pop(pid);

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
        buffer.pop(instr);
        buffer.pop(addr);
        buffer.pop(bare_instr);
        // buffer.pop(value);
        if (DEBUG) {
          std::cout << "LOAD: " << instr << " " << addr << " " << bare_instr << " " << value << std::endl;
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
        buffer.pop(instr);
        buffer.pop(bare_instr);
        buffer.pop(addr);
        if (DEBUG) {
          std::cout << "STORE: " << instr << " " << bare_instr << " " << addr << std::endl;
        }
#if ACTION
        DepMod::store(instr, bare_instr, addr);
#endif
        break;
      };
      case Action::ALLOC: {
        uint64_t addr;
        uint64_t size;
        buffer.pop(addr);
        buffer.pop(size);
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

    while (true) {
      char v;
      while (!queue->pop(v));
      counter++;

      if (counter % 100000000 == 0) {
        std::cout << "Processed " << counter / 1000000 << "M events" << std::endl;
      }
    }
}
