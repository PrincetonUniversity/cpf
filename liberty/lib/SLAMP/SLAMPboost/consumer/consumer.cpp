#include <boost/lockfree/spsc_queue.hpp> // ring buffer

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <cstdint>
#include "ProfilingModules/DependenceModule.h"
#include <iostream>
#include <sstream>

#define DEBUG 0


namespace bip = boost::interprocess;
namespace shm
{
    // typedef bip::allocator<char, bip::managed_shared_memory::segment_manager> char_alloc;
    // typedef bip::basic_string<char, std::char_traits<char>, char_alloc >      shared_string;

    // shared_string, 
    using ring_buffer = boost::lockfree::spsc_queue<uint64_t, boost::lockfree::capacity<65536>>;
}



int main()
{
    // create segment and corresponding allocator
    bip::managed_shared_memory segment(bip::open_or_create, "MySharedMemory", 104857600);
    // shm::char_alloc char_alloc(segment.get_segment_manager());

    shm::ring_buffer *queue = segment.find_or_construct<shm::ring_buffer>("queue")();

    auto counter = 0;
    // shm::shared_string v(char_alloc);

    using Action=DepMod::DepModAction;

    uint32_t loop_id;
    while (true) {
      uint64_t v;
      if (queue->pop(v)) {
        counter++;
        auto action = static_cast<Action>(v);

        uint64_t v1, v2, v3, v4;
        switch (action) {
        case Action::INIT: {
          while (!queue->pop(v1));
          while (!queue->pop(v2));
          if (DEBUG) {
            std::cout << "INIT " << v1 << " " << v2 << std::endl;
          }
          loop_id = v1;
          DepMod::init(v1, v2);
          break;
        };
        case Action::LOAD: {
          while (!queue->pop(v1))
            ;
          while (!queue->pop(v2))
            ;
          while (!queue->pop(v3))
            ;
          while (!queue->pop(v4))
            ;
          if (DEBUG) {
            std::cout << "LOAD " << v1 << " " << v2 << " " << v3 << " " << v4 << std::endl;
          }
          DepMod::load(v1, v2, v3, v4);

          break;
        };
        case Action::STORE: {
          while (!queue->pop(v1));
          while (!queue->pop(v2));
          while (!queue->pop(v3));
          if (DEBUG) {
            std::cout << "STORE " << v1 << " " << v2 << " " << v3 << std::endl;
          }
          DepMod::store(v1, v2, v3);
          break;
        };
        case Action::ALLOC: {
          while (!queue->pop(v1));
          while (!queue->pop(v2));
          DepMod::allocate(reinterpret_cast<void *>(v1), v2);
          if (DEBUG) {
            std::cout << "ALLOC " << v1 << " " << v2 << std::endl;
          }
          break;
        };
        case Action::LOOP_INVOC: {
          DepMod::loop_invoc();
          if (DEBUG) {
            std::cout << "LOOP_INVOC" << std::endl;
          }
          break;
        };
        case Action::LOOP_ITER: {
          DepMod::loop_iter();
          if (DEBUG) {
            std::cout << "LOOP_ITER" << std::endl;
          }
          break;
        };
        case Action::FINISHED: {
          std::stringstream ss;
          ss << "deplog-" << loop_id << ".txt";
          DepMod::fini(ss.str().c_str());
          std::cout << "Finished loop: " << loop_id << " after " << counter
                    << " events" << std::endl;
          break;
        };
        }

        if (counter % 100000000 == 0) {
          std::cout << "Processed " << counter / 1000000 << "M events" << std::endl;
        }
      }
    }
}
