#include <boost/lockfree/spsc_queue.hpp> // ring buffer

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>

#include "atomic_queue/atomic_queue.h"

namespace bip = boost::interprocess;
namespace shm
{
    using Element = char;
    Element constexpr NIL = static_cast<Element>(-1);
    using Queue = atomic_queue::AtomicQueueB<Element, std::allocator<Element>, NIL, false, false, true>;
}

#include <iostream>

int main()
{
    // create segment and corresponding allocator
    bip::managed_shared_memory segment(bip::open_or_create, "MySharedMemory", 104857600);
    auto q = static_cast<void *>(segment.find_or_construct<char>("atomic_queue")[65536]());
    std::cout << q << std::endl;

    shm::Queue *queue = new shm::Queue(65536, q);

    auto counter = 0;
    // shm::shared_string v(char_alloc);
    while (true)
    {
        if (shm::Element v = queue->pop()) {
          counter++;

          if (counter % 1000000 == 0) {
            std::cout << "counter: " << counter << std::endl;
          }
        }
    }
}
